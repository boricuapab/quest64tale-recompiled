"""Import every exported actor in Blender and check UVs, colors and NLA clips."""
import bpy,json,math
from pathlib import Path
root=Path(__file__).resolve().parents[1]
report=json.loads((root/'extracted-assets/actors-extraction-report.json').read_text())
results=[]
for model in report['models']:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps=30
    bpy.ops.import_scene.gltf(filepath=model['path'])
    meshes=[o for o in bpy.data.objects if o.type=='MESH']
    tracks={t.name for o in bpy.data.objects if o.animation_data for t in o.animation_data.nla_tracks}
    assert len(tracks)==model['animations'],(model['name'],len(tracks),model['animations'])
    assert meshes,model['name']
    for o in meshes:
        assert o.data.color_attributes,('no vertex colors',o.name)
        textured=any(m and m.use_nodes and any(n.type=='TEX_IMAGE' for n in m.node_tree.nodes) for m in o.data.materials)
        if textured:assert o.data.uv_layers,('no UVs',o.name)
    animated=[o for o in bpy.data.objects if o.animation_data]
    poses=0
    for name in sorted(tracks):
        for o in animated:
            o.animation_data.action=None
            for t in o.animation_data.nla_tracks:t.mute=t.name!=name
        ends=[t.strips[0].frame_end for o in animated for t in o.animation_data.nla_tracks if t.name==name]
        end=max(ends)
        for frame in [1,(end+1)/2,end]:
            bpy.context.scene.frame_set(int(frame),subframe=frame-int(frame))
            for o in bpy.data.objects:
                assert all(math.isfinite(v) for row in o.matrix_world for v in row),(model['name'],name,frame,o.name)
            poses+=1
    results.append(dict(name=model['name'],mesh_objects=len(meshes),nla_tracks=len(tracks),sampled_poses=poses,status='passed'))
    print('BLENDER PASS',model['name'],len(tracks),'NLA clips',flush=True)
    (root/'extracted-assets/blender-actor-validation.json').write_text(json.dumps(results,indent=2))
print('ALL ACTORS PASSED',len(results),flush=True)
