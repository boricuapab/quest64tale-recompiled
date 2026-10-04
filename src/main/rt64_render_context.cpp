#include <memory>
#include <cstring>
#include <variant>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>

#define HLSL_CPU
#include "hle/rt64_application.h"
#include "gbi/rt64_gbi_f3d.h"
#include "gbi/rt64_gbi_f3dex.h"
#include "gbi/rt64_gbi_rdp.h"
#include "rt64_render_hooks.h"
#include "overloaded.h"

#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"

#include "zelda_render.h"
#include "recomp_ui.h"
#include "concurrentqueue.h"

static RT64::UserConfiguration::Antialiasing device_max_msaa = RT64::UserConfiguration::Antialiasing::None;
static bool sample_positions_supported = false;
static bool high_precision_fb_enabled = false;

static uint8_t DMEM[0x1000];
static uint8_t IMEM[0x1000];

// Brian's model table supplies stable part identities. RT64's automatic
// matching can pair the thin, similar hair polygons with another transform.
// Tag the model lists, without changing the original matrices or vertices.
namespace {
thread_local std::vector<size_t> brian_group_depths;
thread_local std::vector<size_t> fade_group_depths;
thread_local unsigned brian_part_count = 0, shadow_quad_count = 0;
thread_local int quest64_hud_mode=0;
void quest64_noop(RT64::State* state,RT64::DisplayList** dl){
    if((*dl)->w1==0x51455854){state->extended.extendRDRAM=true;return;}
    if((*dl)->w1==0x51485544)quest64_hud_mode=1;
    else if((*dl)->w1==0x514C4546)quest64_hud_mode=2;
    else if((*dl)->w1==0x51524947)quest64_hud_mode=3;
    else if((*dl)->w1==0x51574F52)quest64_hud_mode=0;
    else return;
    // The compass needle is geometry; give it the same origin as its sprites.
    state->rsp->extended.viewportOrigin=quest64_hud_mode==3?G_EX_ORIGIN_RIGHT:G_EX_ORIGIN_NONE;
    state->rsp->viewportChanged=true;
    state->rsp->modelViewProjChanged=true;
}
void quest64_rectangle(RT64::State* state,RT64::DisplayList** dl){
    auto* rdp=state->rdp.get();
    const uint32_t op=(*dl)->w0>>24;
    const int x0=(*dl)->p1(12,12),x1=(*dl)->p0(12,12);
    const int y0=(*dl)->p1(0,12),y1=(*dl)->p0(0,12);
    // Clear overscan as well as the world. Old edge-anchored HUD pixels must
    // never survive into the next frame or a different aspect/HUD setting.
    if(op==0xF6&&quest64_hud_mode==0&&x0==8*4&&x1==311*4&&y0==8*4&&y1==231*4){
        const auto command=**dl;
        (*dl)->w0=0xF6000000|(319*4<<12)|(239*4);(*dl)->w1=0;
        RT64::ExtendedAlignment clear;
        rdp->setRectAlign(clear);rdp->setScissor(0,0,0,320*4,240*4,clear);
        RT64::GBI_RDP::fillRect(state,dl);
        **dl=command;
        return;
    }
    int origin=G_EX_ORIGIN_NONE;
    if(quest64_hud_mode==2)origin=G_EX_ORIGIN_LEFT;
    else if(quest64_hud_mode==3)origin=G_EX_ORIGIN_RIGHT;
    const auto saved=rdp->extended;
    const bool aligned=origin!=G_EX_ORIGIN_NONE;
    if(aligned){
        RT64::ExtendedAlignment rect;
        rect.leftOrigin=uint16_t(origin);
        rect.rightOrigin=uint16_t(origin);
        rect.leftOffset=rect.leftOrigin==G_EX_ORIGIN_RIGHT?-320*4:0;
        rect.rightOffset=rect.rightOrigin==G_EX_ORIGIN_RIGHT?-320*4:0;
        rdp->setRectAlign(rect);
        RT64::ExtendedAlignment scissor;
        scissor.leftOrigin=G_EX_ORIGIN_LEFT;scissor.rightOrigin=G_EX_ORIGIN_RIGHT;scissor.rightOffset=-320*4;
        rdp->pushScissor();rdp->setScissor(0,8*4,8*4,312*4,232*4,scissor);
    }
    if(op==0xF6)RT64::GBI_RDP::fillRect(state,dl);
    else if(op==0xE5)RT64::GBI_RDP::texrectFlip(state,dl);
    else RT64::GBI_RDP::texrect(state,dl);
    if(aligned){rdp->extended=saved;rdp->popScissor();}
}
void quest64_scissor(RT64::State* state,RT64::DisplayList** dl){
    const auto command=**dl;
    if((*dl)->p0(12,12)==32&&(*dl)->p1(12,12)==1248){
        (*dl)->w0 &= ~(0xFFFU<<12);
        (*dl)->w1=((*dl)->w1&~(0xFFFU<<12))|(1280U<<12);
        // The original viewport has overscan margins. Widen its clipping
        // volume so RT64 recognizes the full framebuffer as one projection.
        state->rsp->clipRatios[0]=2;state->rsp->clipRatios[2]=-2;
        state->rsp->viewportChanged=true;
        state->rsp->modelViewProjChanged=true;
    }
    RT64::GBI_RDP::setScissor(state,dl);
    **dl=command;
}
bool shadow_depth_diagnostic() {
    static const bool enabled = std::getenv("QUEST64_SHADOW_DEPTH_DIAGNOSTIC") != nullptr;
    return enabled;
}

uint32_t rdram_word(RT64::State* state, uint32_t offset) {
    uint32_t value;
    std::memcpy(&value, state->fromRDRAM(offset), sizeof(value));
    return value;
}

void quest64_model_list(RT64::State* state, RT64::DisplayList** dl) {
    const uint32_t address = state->rsp->fromSegmentedMasked((*dl)->w1);
    const bool call = (*dl)->p0(16, 1) == 0;
    int part = -1;
    const bool fade=call&&address==0x4D4F0;
    if(fade){
        // Door fades are fullscreen geometry, rather than a centered menu.
        state->rsp->matrixId(0x51464144,true,true,false,
            G_EX_COMPONENT_SKIP,G_EX_COMPONENT_SKIP,G_EX_COMPONENT_SKIP,
            G_EX_COMPONENT_SKIP,G_EX_COMPONENT_SKIP,G_EX_COMPONENT_SKIP,
            G_EX_COMPONENT_SKIP,G_EX_COMPONENT_SKIP,G_EX_COMPONENT_SKIP,
            G_EX_ORDER_LINEAR,G_EX_ASPECT_STRETCH,G_EX_EDIT_NONE,false,false);
        state->rsp->modelViewProjChanged=true;
    }
    // Verify the loaded Brian bank before interpreting its model table.
    if (call && rdram_word(state, 0x20606C) == 0x80206000) {
        for (int i = 0; i < 32; ++i) {
            const uint32_t entry = rdram_word(state, 0x206000 + i * 4);
            if (entry == 0) break;
            if ((entry & 0x7FFFFF) == address) { part = i; break; }
        }
    }
    if (part >= 0) {
        ++brian_part_count;
        // Component-wise matrix interpolation preserves the native shear and
        // signed scale without introducing quaternion decomposition choices.
        state->rsp->matrixId(0xB1000000U + part, true, false, false,
            G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
            G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
            G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
            G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
            G_EX_ORDER_LINEAR, G_EX_ASPECT_AUTO, G_EX_EDIT_NONE, false, false);
        state->rsp->modelViewProjChanged = true;
    }
    RT64::GBI_F3D::runDl(state, dl);
    if (part >= 0) brian_group_depths.push_back(state->returnAddressStack.size());
    if(fade)fade_group_depths.push_back(state->returnAddressStack.size());
}

void quest64_end_list(RT64::State* state, RT64::DisplayList** dl) {
    if(!fade_group_depths.empty()&&fade_group_depths.back()==state->returnAddressStack.size()){
        state->rsp->popMatrixId(1,true);
        state->rsp->modelViewProjChanged=true;
        fade_group_depths.pop_back();
    }
    if (!brian_group_depths.empty() &&
        brian_group_depths.back() == state->returnAddressStack.size()) {
        state->rsp->popMatrixId(1, false);
        state->rsp->modelViewProjChanged = true;
        brian_group_depths.pop_back();
    }
    RT64::GBI_F3D::endDl(state, dl);
}

void quest64_vertex(RT64::State* state, RT64::DisplayList** dl) {
    if (state->rsp->fromSegmentedMasked((*dl)->w1) == 0x96A80) ++shadow_quad_count;
    RT64::GBI_F3DEX::vertex(state, dl);
}

void quest64_render_mode(RT64::State* state, RT64::DisplayList** dl) {
    if (shadow_depth_diagnostic() && (*dl)->w0 == 0xB900031D &&
        (*dl)->w1 == 0xC8104E50 &&
        reinterpret_cast<uint8_t*>(*dl) == state->fromRDRAM(0x96AF8)) {
        // Diagnostic only: distinguish depth rejection from absent/culled
        // geometry. Preserve blending, and never change the game's RDRAM.
        auto command = **dl;
        command.w1 &= ~(0xC00U | 0x10U);
        auto* local = &command;
        RT64::GBI_F3D::setOtherModeL(state, &local);
        return;
    }
    RT64::GBI_F3D::setOtherModeL(state, dl);
}

void trace_quest64_frame(RT64::State* state) {
    // Opt-in diagnostics for comparing native shadow submission and rendered
    // geometry. No ROM bytes, texture pixels, or machine paths are recorded.
    static FILE* trace = std::getenv("QUEST64_RENDER_TRACE") ?
        std::fopen("quest64-render-trace.csv", "w") : nullptr;
    static unsigned frame = 0;
    if (!trace || (++frame % 30) != 0) return;
    if (frame == 30) std::fprintf(trace,"frame,brian_parts,shadow_quads,queued_shadows,nearest_shadow_distance,native_shadow_mode,remaining_part_groups,shadow_depth_bypass\n");
    auto read_float = [state](uint32_t offset) {
        const uint32_t word = rdram_word(state, offset);
        float value; std::memcpy(&value, &word, sizeof(value)); return value;
    };
    const uint32_t count = rdram_word(state, 0x862D0);
    const float px = read_float(0x7BACC), pz = read_float(0x7BAD4);
    float nearest = -1.0f;
    if (count <= 64) for (uint32_t i = 0; i < count; ++i) {
        const float dx = read_float(0x85BD0 + i * 28) - px;
        const float dz = read_float(0x85BD8 + i * 28) - pz;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (nearest < 0 || distance < nearest) nearest = distance;
    }
    std::fprintf(trace,"%u,%u,%u,%u,%.6f,%08X,%zu,%d\n",frame,brian_part_count,
        shadow_quad_count,count,nearest,rdram_word(state,0x96AFC),brian_group_depths.size(),shadow_depth_diagnostic());
    std::fflush(trace);
}
}

struct TexturePackEnableAction {
    std::string mod_id;
};

struct TexturePackDisableAction {
    std::string mod_id;
};

struct TexturePackSecondaryEnableAction {
    std::string mod_id;
};

struct TexturePackSecondaryDisableAction {
    std::string mod_id;
};

struct TexturePackUpdateAction {
};

using TexturePackAction = std::variant<TexturePackEnableAction, TexturePackDisableAction, TexturePackSecondaryEnableAction, TexturePackSecondaryDisableAction, TexturePackUpdateAction>;

static moodycamel::ConcurrentQueue<TexturePackAction> texture_pack_action_queue;

unsigned int MI_INTR_REG = 0;

unsigned int DPC_START_REG = 0;
unsigned int DPC_END_REG = 0;
unsigned int DPC_CURRENT_REG = 0;
unsigned int DPC_STATUS_REG = 0;
unsigned int DPC_CLOCK_REG = 0;
unsigned int DPC_BUFBUSY_REG = 0;
unsigned int DPC_PIPEBUSY_REG = 0;
unsigned int DPC_TMEM_REG = 0;

void dummy_check_interrupts() {}

RT64::UserConfiguration::Antialiasing compute_max_supported_aa(plume::RenderSampleCounts bits) {
    if (bits & plume::RenderSampleCount::Bits::COUNT_2) {
        if (bits & plume::RenderSampleCount::Bits::COUNT_4) {
            if (bits & plume::RenderSampleCount::Bits::COUNT_8) {
                return RT64::UserConfiguration::Antialiasing::MSAA8X;
            }
            return RT64::UserConfiguration::Antialiasing::MSAA4X;
        }
        return RT64::UserConfiguration::Antialiasing::MSAA2X;
    };
    return RT64::UserConfiguration::Antialiasing::None;
}

RT64::UserConfiguration::AspectRatio to_rt64(ultramodern::renderer::AspectRatio option) {
    switch (option) {
        case ultramodern::renderer::AspectRatio::Original:
            return RT64::UserConfiguration::AspectRatio::Original;
        case ultramodern::renderer::AspectRatio::Expand:
            return RT64::UserConfiguration::AspectRatio::Expand;
        case ultramodern::renderer::AspectRatio::Manual:
            return RT64::UserConfiguration::AspectRatio::Manual;
        case ultramodern::renderer::AspectRatio::OptionCount:
            return RT64::UserConfiguration::AspectRatio::OptionCount;
    }
}

RT64::UserConfiguration::Antialiasing to_rt64(ultramodern::renderer::Antialiasing option) {
    switch (option) {
        case ultramodern::renderer::Antialiasing::None:
            return RT64::UserConfiguration::Antialiasing::None;
        case ultramodern::renderer::Antialiasing::MSAA2X:
            return RT64::UserConfiguration::Antialiasing::MSAA2X;
        case ultramodern::renderer::Antialiasing::MSAA4X:
            return RT64::UserConfiguration::Antialiasing::MSAA4X;
        case ultramodern::renderer::Antialiasing::MSAA8X:
            return RT64::UserConfiguration::Antialiasing::MSAA8X;
        case ultramodern::renderer::Antialiasing::OptionCount:
            return RT64::UserConfiguration::Antialiasing::OptionCount;
    }
}

RT64::UserConfiguration::RefreshRate to_rt64(ultramodern::renderer::RefreshRate option) {
    switch (option) {
        case ultramodern::renderer::RefreshRate::Original:
            return RT64::UserConfiguration::RefreshRate::Original;
        case ultramodern::renderer::RefreshRate::Display:
            return RT64::UserConfiguration::RefreshRate::Display;
        case ultramodern::renderer::RefreshRate::Manual:
            return RT64::UserConfiguration::RefreshRate::Manual;
        case ultramodern::renderer::RefreshRate::OptionCount:
            return RT64::UserConfiguration::RefreshRate::OptionCount;
    }
}

RT64::UserConfiguration::InternalColorFormat to_rt64(ultramodern::renderer::HighPrecisionFramebuffer option) {
    switch (option) {
        case ultramodern::renderer::HighPrecisionFramebuffer::Off:
            return RT64::UserConfiguration::InternalColorFormat::Standard;
        case ultramodern::renderer::HighPrecisionFramebuffer::On:
            return RT64::UserConfiguration::InternalColorFormat::High;
        case ultramodern::renderer::HighPrecisionFramebuffer::Auto:
            return RT64::UserConfiguration::InternalColorFormat::Automatic;
        case ultramodern::renderer::HighPrecisionFramebuffer::OptionCount:
            return RT64::UserConfiguration::InternalColorFormat::OptionCount;
    }
}

void set_application_user_config(RT64::Application* application, const ultramodern::renderer::GraphicsConfig& config) {
    switch (config.res_option) {
        default:
        case ultramodern::renderer::Resolution::Auto:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::WindowIntegerScale;
            application->userConfig.downsampleMultiplier = 1;
            break;
        case ultramodern::renderer::Resolution::Original:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
            application->userConfig.resolutionMultiplier = std::max(config.ds_option, 1);
            application->userConfig.downsampleMultiplier = std::max(config.ds_option, 1);
            break;
        case ultramodern::renderer::Resolution::Original2x:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
            application->userConfig.resolutionMultiplier = 2.0 * std::max(config.ds_option, 1);
            application->userConfig.downsampleMultiplier = std::max(config.ds_option, 1);
            break;
    }

    switch (config.hr_option) {
        default:
        case ultramodern::renderer::HUDRatioMode::Original:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Original;
            break;
        case ultramodern::renderer::HUDRatioMode::Clamp16x9:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Manual;
            application->userConfig.extAspectTarget = 16.0/9.0;
            break;
        case ultramodern::renderer::HUDRatioMode::Full:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Expand;
            break;
    }

    application->userConfig.aspectRatio = to_rt64(config.ar_option);
    application->userConfig.aspectTarget = 16.0 / 9.0;
    application->userConfig.antialiasing = to_rt64(config.msaa_option);
    application->userConfig.refreshRate = to_rt64(config.rr_option);
    application->userConfig.refreshRateTarget = config.rr_manual_value;
    application->userConfig.internalColorFormat = to_rt64(config.hpfb_option);
    application->userConfig.displayBuffering = RT64::UserConfiguration::DisplayBuffering::Triple;
}

ultramodern::renderer::SetupResult map_setup_result(RT64::Application::SetupResult rt64_result) {
    switch (rt64_result) {
        case RT64::Application::SetupResult::Success:
            return ultramodern::renderer::SetupResult::Success;
        case RT64::Application::SetupResult::DynamicLibrariesNotFound:
            return ultramodern::renderer::SetupResult::DynamicLibrariesNotFound;
        case RT64::Application::SetupResult::InvalidGraphicsAPI:
            return ultramodern::renderer::SetupResult::InvalidGraphicsAPI;
        case RT64::Application::SetupResult::GraphicsAPINotFound:
            return ultramodern::renderer::SetupResult::GraphicsAPINotFound;
        case RT64::Application::SetupResult::GraphicsDeviceNotFound:
            return ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
    }

    fprintf(stderr, "Unhandled `RT64::Application::SetupResult` ?\n");
    assert(false);
    std::exit(EXIT_FAILURE);
}

ultramodern::renderer::GraphicsApi map_graphics_api(RT64::UserConfiguration::GraphicsAPI api) {
    switch (api) {
        case RT64::UserConfiguration::GraphicsAPI::D3D12:
            return ultramodern::renderer::GraphicsApi::D3D12;
        case RT64::UserConfiguration::GraphicsAPI::Vulkan:
            return ultramodern::renderer::GraphicsApi::Vulkan;
        case RT64::UserConfiguration::GraphicsAPI::Metal:
            return ultramodern::renderer::GraphicsApi::Metal;
        case RT64::UserConfiguration::GraphicsAPI::Automatic:
            return ultramodern::renderer::GraphicsApi::Auto;
    }

    fprintf(stderr, "Unhandled `RT64::UserConfiguration::GraphicsAPI` ?\n");
    assert(false);
    std::exit(EXIT_FAILURE);
}

zelda64::renderer::RT64Context::RT64Context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool debug) {
    static unsigned char dummy_rom_header[0x40];
    recompui::set_render_hooks();

    // Set up the RT64 application core fields.
    RT64::Application::Core appCore{};
#if defined(_WIN32)
    appCore.window = window_handle.window;
#elif defined(__linux__) || defined(__ANDROID__)
    appCore.window = window_handle;
#elif defined(__APPLE__)
    appCore.window.window = window_handle.window;
    appCore.window.view = window_handle.view;
#endif

    appCore.checkInterrupts = dummy_check_interrupts;

    appCore.HEADER = dummy_rom_header;
    appCore.RDRAM = rdram;
    appCore.DMEM = DMEM;
    appCore.IMEM = IMEM;

    appCore.MI_INTR_REG = &MI_INTR_REG;

    appCore.DPC_START_REG = &DPC_START_REG;
    appCore.DPC_END_REG = &DPC_END_REG;
    appCore.DPC_CURRENT_REG = &DPC_CURRENT_REG;
    appCore.DPC_STATUS_REG = &DPC_STATUS_REG;
    appCore.DPC_CLOCK_REG = &DPC_CLOCK_REG;
    appCore.DPC_BUFBUSY_REG = &DPC_BUFBUSY_REG;
    appCore.DPC_PIPEBUSY_REG = &DPC_PIPEBUSY_REG;
    appCore.DPC_TMEM_REG = &DPC_TMEM_REG;

    ultramodern::renderer::ViRegs* vi_regs = ultramodern::renderer::get_vi_regs();

    appCore.VI_STATUS_REG = &vi_regs->VI_STATUS_REG;
    appCore.VI_ORIGIN_REG = &vi_regs->VI_ORIGIN_REG;
    appCore.VI_WIDTH_REG = &vi_regs->VI_WIDTH_REG;
    appCore.VI_INTR_REG = &vi_regs->VI_INTR_REG;
    appCore.VI_V_CURRENT_LINE_REG = &vi_regs->VI_V_CURRENT_LINE_REG;
    appCore.VI_TIMING_REG = &vi_regs->VI_TIMING_REG;
    appCore.VI_V_SYNC_REG = &vi_regs->VI_V_SYNC_REG;
    appCore.VI_H_SYNC_REG = &vi_regs->VI_H_SYNC_REG;
    appCore.VI_LEAP_REG = &vi_regs->VI_LEAP_REG;
    appCore.VI_H_START_REG = &vi_regs->VI_H_START_REG;
    appCore.VI_V_START_REG = &vi_regs->VI_V_START_REG;
    appCore.VI_V_BURST_REG = &vi_regs->VI_V_BURST_REG;
    appCore.VI_X_SCALE_REG = &vi_regs->VI_X_SCALE_REG;
    appCore.VI_Y_SCALE_REG = &vi_regs->VI_Y_SCALE_REG;

    // Set up the RT64 application configuration fields.
    RT64::ApplicationConfiguration appConfig;
    appConfig.useConfigurationFile = false;

    // Create the RT64 application.
    app = std::make_unique<RT64::Application>(appCore, appConfig);

    // Set initial user config settings based on the current settings.
    auto& cur_config = ultramodern::renderer::get_graphics_config();
    set_application_user_config(app.get(), cur_config);
    app->userConfig.developerMode = debug;
    // Force gbi depth branches to prevent LODs from kicking in.
    app->enhancementConfig.f3dex.forceBranch = true;
    // Avoid adding post-blend noise beyond the game's native dithering.
    app->emulatorConfig.dither.postBlendNoise = false;
    app->emulatorConfig.dither.postBlendNoiseNegative = false;
    // Scale LODs based on the output resolution.
    app->enhancementConfig.textureLOD.scale = true;
    // Pick an API if the user has set an override.
    switch (cur_config.api_option) {
        case ultramodern::renderer::GraphicsApi::D3D12:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::D3D12;
            break;
        case ultramodern::renderer::GraphicsApi::Vulkan:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Vulkan;
            break;
        case ultramodern::renderer::GraphicsApi::Metal:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Metal;
            break;
        case ultramodern::renderer::GraphicsApi::Auto:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Automatic;
            break;
    }

    // Set up the RT64 application.
    uint32_t thread_id = 0;
#ifdef _WIN32
    thread_id = window_handle.thread_id;
#endif
    setup_result = map_setup_result(app->setup(thread_id));
    // Get the API that RT64 chose.
    chosen_api = map_graphics_api(app->chosenGraphicsAPI);
    if (setup_result != ultramodern::renderer::SetupResult::Success) {
        app = nullptr;
        return;
    }

    // Set the application's fullscreen state.
    app->setFullScreen(cur_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);

    // Check if the selected device actually supports MSAA sample positions and MSAA for for the formats that will be used
    // and downgrade the configuration accordingly.
    if (app->device->getCapabilities().sampleLocations) {
        plume::RenderSampleCounts color_sample_counts = app->device->getSampleCountsSupported(plume::RenderFormat::R8G8B8A8_UNORM);
        plume::RenderSampleCounts depth_sample_counts = app->device->getSampleCountsSupported(plume::RenderFormat::D32_FLOAT);
        plume::RenderSampleCounts common_sample_counts = color_sample_counts & depth_sample_counts;
        device_max_msaa = compute_max_supported_aa(common_sample_counts);
        sample_positions_supported = true;
    }
    else {
        device_max_msaa = RT64::UserConfiguration::Antialiasing::None;
        sample_positions_supported = false;
    }

    high_precision_fb_enabled = app->shaderLibrary->usesHDR;
}

zelda64::renderer::RT64Context::~RT64Context() = default;

void zelda64::renderer::RT64Context::send_dl(const OSTask* task) {
    check_texture_pack_actions();
    app->state->rsp->reset();
    app->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFF, task->t.ucode_data & 0x3FFFFFF, true);
    brian_group_depths.clear();
    fade_group_depths.clear();
    brian_part_count = shadow_quad_count = 0;
    quest64_hud_mode=0;
    auto* gbi = app->interpreter->hleGBI;
    if (gbi->ucode == RT64::GBIUCode::F3DEX) {
        gbi->map[0x06] = quest64_model_list;
        gbi->map[0xB8] = quest64_end_list;
        gbi->map[0x04] = quest64_vertex;
        gbi->map[0xB9] = quest64_render_mode;
        gbi->map[0x00] = quest64_noop;
        gbi->map[0xE4] = quest64_rectangle;
        gbi->map[0xE5] = quest64_rectangle;
        gbi->map[0xF6] = quest64_rectangle;
        gbi->map[0xED] = quest64_scissor;
    }
    app->processDisplayLists(app->core.RDRAM, task->t.data_ptr & 0x3FFFFFF, 0, true);
    trace_quest64_frame(app->state.get());
}

void zelda64::renderer::RT64Context::update_screen() {
    app->updateScreen();
}

void zelda64::renderer::RT64Context::shutdown() {
    if (app != nullptr) {
        app->end();
    }
}

bool zelda64::renderer::RT64Context::update_config(const ultramodern::renderer::GraphicsConfig& old_config, const ultramodern::renderer::GraphicsConfig& new_config) {
    if (old_config == new_config) {
        return false;
    }

    if (new_config.wm_option != old_config.wm_option) {
        app->setFullScreen(new_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);
    }

    set_application_user_config(app.get(), new_config);

    app->updateUserConfig(true);

    if (new_config.msaa_option != old_config.msaa_option) {
        app->updateMultisampling();
    }
    return true;
}

void zelda64::renderer::RT64Context::enable_instant_present() {
    // Enable the present early presentation mode for minimal latency.
    app->enhancementConfig.presentation.mode = RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly;

    app->updateEnhancementConfig();
}

uint32_t zelda64::renderer::RT64Context::get_display_framerate() const {
    return app->presentQueue->ext.sharedResources->swapChainRate;
}

float zelda64::renderer::RT64Context::get_resolution_scale() const {
    constexpr int ReferenceHeight = 240;
    switch (app->userConfig.resolution) {
        case RT64::UserConfiguration::Resolution::WindowIntegerScale:
            if (app->sharedQueueResources->swapChainHeight > 0) {
                return std::max(float((app->sharedQueueResources->swapChainHeight + ReferenceHeight - 1) / ReferenceHeight), 1.0f);
            }
            else {
                return 1.0f;
            }
        case RT64::UserConfiguration::Resolution::Manual:
            return float(app->userConfig.resolutionMultiplier);
        case RT64::UserConfiguration::Resolution::Original:
        default:
            return 1.0f;
    }
}

void zelda64::renderer::RT64Context::check_texture_pack_actions() {
    bool packs_changed = false;
    TexturePackAction cur_action;
    while (texture_pack_action_queue.try_dequeue(cur_action)) {
        std::visit(overloaded{
            [&](TexturePackDisableAction &to_disable) {
                enabled_texture_packs.erase(to_disable.mod_id);
                packs_changed = true;
            },
            [&](TexturePackEnableAction &to_enable) {
                enabled_texture_packs.insert(to_enable.mod_id);
                packs_changed = true;
            },
            [&](TexturePackSecondaryDisableAction &to_override_disable) {
                secondary_disabled_texture_packs.insert(to_override_disable.mod_id);
                packs_changed = true;
            },
            [&](TexturePackSecondaryEnableAction &to_override_enable) {
                secondary_disabled_texture_packs.erase(to_override_enable.mod_id);
                packs_changed = true;
            },
            [&](TexturePackUpdateAction &) {
                packs_changed = true;
            }
        }, cur_action);
    }

    // If any packs were disabled, unload all packs and load all the active ones.
    if (packs_changed) {
        // Sort the enabled texture packs in reverse order so that earlier ones override later ones.
        std::vector<std::string> sorted_texture_packs{};
        sorted_texture_packs.reserve(enabled_texture_packs.size());
        for (const std::string& mod : enabled_texture_packs) {
            if (!secondary_disabled_texture_packs.contains(mod)) {
                sorted_texture_packs.emplace_back(mod);
            }
        }

        std::sort(sorted_texture_packs.begin(), sorted_texture_packs.end(),
            [](const std::string& lhs, const std::string& rhs) {
                return recomp::mods::get_mod_order_index(lhs) > recomp::mods::get_mod_order_index(rhs);
            }
        );

        // Build the path list from the sorted mod list.
        std::vector<RT64::ReplacementDirectory> replacement_directories;
        replacement_directories.reserve(enabled_texture_packs.size());
        for (const std::string &mod_id : sorted_texture_packs) {
            replacement_directories.emplace_back(RT64::ReplacementDirectory(recomp::mods::get_mod_filename(mod_id)));
        }

        if (!replacement_directories.empty()) {
            app->textureCache->loadReplacementDirectories(replacement_directories);
        }
        else {
            app->textureCache->clearReplacementDirectories();
        }
    }
}

RT64::UserConfiguration::Antialiasing zelda64::renderer::RT64MaxMSAA() {
    return device_max_msaa;
}

std::unique_ptr<ultramodern::renderer::RendererContext> zelda64::renderer::create_render_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
    return std::make_unique<zelda64::renderer::RT64Context>(rdram, window_handle, developer_mode);
}

bool zelda64::renderer::RT64SamplePositionsSupported() {
    return sample_positions_supported;
}

bool zelda64::renderer::RT64HighPrecisionFBEnabled() {
    return high_precision_fb_enabled;
}

void zelda64::renderer::trigger_texture_pack_update() {
    texture_pack_action_queue.enqueue(TexturePackUpdateAction{});
}

void zelda64::renderer::enable_texture_pack(const recomp::mods::ModContext& context, const recomp::mods::ModHandle& mod) {
    texture_pack_action_queue.enqueue(TexturePackEnableAction{mod.manifest.mod_id});

    // Check for the texture pack enabled config option.
    const recomp::mods::ConfigSchema& config_schema = context.get_mod_config_schema(mod.manifest.mod_id);
    auto find_it = config_schema.options_by_id.find(zelda64::renderer::special_option_texture_pack_enabled);
    if (find_it != config_schema.options_by_id.end()) {
        const recomp::mods::ConfigOption& config_option = config_schema.options[find_it->second];

        if (is_texture_pack_enable_config_option(config_option, false)) {
            recomp::mods::ConfigValueVariant value_variant = context.get_mod_config_value(mod.manifest.mod_id, config_option.id);
            uint32_t value;
            if (uint32_t* value_ptr = std::get_if<uint32_t>(&value_variant)) {
                value = *value_ptr;
            }
            else {
                value = 0;
            }

            if (value) {
                zelda64::renderer::secondary_enable_texture_pack(mod.manifest.mod_id);
            }
            else {
                zelda64::renderer::secondary_disable_texture_pack(mod.manifest.mod_id);
            }
        }
    }
}

void zelda64::renderer::disable_texture_pack(const recomp::mods::ModHandle& mod) {
    texture_pack_action_queue.enqueue(TexturePackDisableAction{mod.manifest.mod_id});
}

void zelda64::renderer::secondary_enable_texture_pack(const std::string& mod_id) {
    texture_pack_action_queue.enqueue(TexturePackSecondaryEnableAction{mod_id});
}

void zelda64::renderer::secondary_disable_texture_pack(const std::string& mod_id) {
    texture_pack_action_queue.enqueue(TexturePackSecondaryDisableAction{mod_id});
}


// HD texture enable option. Must be an enum with two options.
// The first option is treated as disabled and the second option is treated as enabled.
bool zelda64::renderer::is_texture_pack_enable_config_option(const recomp::mods::ConfigOption& option, bool show_errors) {
    if (option.id == zelda64::renderer::special_option_texture_pack_enabled) {
        if (option.type != recomp::mods::ConfigOptionType::Enum) {
            if (show_errors) {
                recompui::message_box(("Mod has the special config option id for enabling an HD texture pack (\"" + zelda64::renderer::special_option_texture_pack_enabled + "\"), but the config option is not an enum.").c_str());
            }
            return false;
        }

        const recomp::mods::ConfigOptionEnum &option_enum = std::get<recomp::mods::ConfigOptionEnum>(option.variant);
        if (option_enum.options.size() != 2) {
            if (show_errors) {
                recompui::message_box(("Mod has the special config option id for enabling an HD texture pack (\"" + zelda64::renderer::special_option_texture_pack_enabled + "\"), but the config option doesn't have exactly 2 values.").c_str());
            }
            return false;
        }

        return true;
    }
    return false;
}
