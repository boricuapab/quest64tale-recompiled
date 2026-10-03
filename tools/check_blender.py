import bpy,json,numpy as np
from pathlib import Path
root=Path(__file__).resolve().parents[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.context.scene.render.fps=30
bpy.ops.import_scene.gltf(filepath=str(root/'extracted-assets/characters/brian/brian.glb'))
meshes=[o for o in bpy.data.objects if o.type=='MESH']
tracks=set()
for o in bpy.data.objects:
    if o.animation_data:
        tracks.update(t.name for t in o.animation_data.nla_tracks)
report=dict(mesh_objects=len(meshes),uv_layers={o.name:len(o.data.uv_layers) for o in meshes},nla_tracks=sorted(tracks),actions=len(bpy.data.actions))
report['track_state']=[dict(object=o.name,active_action=o.animation_data.action.name if o.animation_data.action else None,tracks=[dict(name=t.name,mute=t.mute,start=t.strips[0].frame_start) for t in o.animation_data.nla_tracks]) for o in bpy.data.objects if o.animation_data][:1]
assert len(tracks)==30,report
assert all(o.data.uv_layers for o in meshes if any(m and any(n.type=='TEX_IMAGE' for n in m.node_tree.nodes) for m in o.data.materials)),report
references=np.load(root/'extracted-assets/characters/brian/animation-reference.npz')
animated=[o for o in bpy.data.objects if o.animation_data]
conversion=np.array([[1,0,0,0],[0,0,-1,0],[0,1,0,0],[0,0,0,1]],dtype=float)
units=np.diag([.01,.01,.01,1]);inverse=np.linalg.inv(conversion)
checked=0;maximum_error=0
for name in references.files:
    for o in animated:
        o.animation_data.action=None
        for track in o.animation_data.nla_tracks:track.mute=track.name!=name
    matrices=references[name]
    for frame in range(len(matrices)):
        # Check integer frames and intermediate scrub positions.
        for subframe in (0.,.5):
            bpy.context.scene.frame_set(frame+1,subframe=subframe)
            for j in range(24):
                actual=np.asarray(bpy.data.objects[f'joint_{j:02d}_basis'].matrix_world)
                expected=conversion@units@matrices[frame,j]@inverse
                error=float(np.max(np.abs(actual-expected)))
                maximum_error=max(maximum_error,error)
                assert error<2e-4,(name,frame,subframe,j,error)
            checked+=1
cursor=1
sequence=[]
for action in bpy.data.actions:
    for layer in action.layers:
        for action_strip in layer.strips:
            for bag in action_strip.channelbags:
                for curve in bag.fcurves:
                    last=curve.keyframe_points[-1]
                    time,value=last.co
                    added=curve.keyframe_points.insert(time+1,value)
                    added.interpolation='CONSTANT'
for name in references.files:
    duration=len(references[name])-1
    for o in animated:
        for track in o.animation_data.nla_tracks:
            if track.name==name:
                track.mute=False
                strip=track.strips[0]
                strip.frame_start=cursor
                strip.action_frame_end=duration+2
                strip.frame_end=cursor+duration+1
                strip.extrapolation='NOTHING'
                strip.blend_type='REPLACE'
    bpy.context.scene.timeline_markers.new(name,frame=cursor)
    sequence.append(dict(animation=name,start=cursor,end=cursor+duration))
    cursor+=duration+1
bpy.context.scene.frame_set(1)
bpy.context.scene.frame_end=cursor-1
for item in sequence:
    matrices=references[item['animation']]
    for frame in sorted({0,len(matrices)//2,len(matrices)-1}):
        bpy.context.scene.frame_set(item['start']+frame,subframe=.5)
        for j in range(24):
            actual=np.asarray(bpy.data.objects[f'joint_{j:02d}_basis'].matrix_world)
            expected=conversion@units@matrices[frame,j]@inverse
            assert np.max(np.abs(actual-expected))<2e-4,(item['animation'],frame,'NLA sequence')
bpy.context.scene.frame_set(1)
report.update(checked_poses=checked,maximum_matrix_error=maximum_error,nla_sequence=sequence)
(root/'extracted-assets/blender-verification.json').write_text(json.dumps(report,indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(root/'extracted-assets/characters/brian/brian.blend'))
print(json.dumps(report))
