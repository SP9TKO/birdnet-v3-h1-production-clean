"""Frozen canonical-M3 -> V2 control and minimal prospective V3 construction."""
from pathlib import Path
import sys,json,hashlib,struct,copy
import numpy as np
O=Path(__file__).resolve().parent;R=O.parents[1];V2=R/'.development-work/m4-range-closure'
sys.path.insert(0,str(V2/'construction'))
from h1_model import configure_tensorflow,assert_runtime_environment
from m4_componentized_common import source_binding,slice_stages,make_components,save_components,frontend_value,run_stage_slice,generated_waveforms
from m4_componentized_candidate_common import convert_component
from m4_n1_freeze_common import float32_from_bits
from m4_n1_flatbuffer import unpack_model,pack_model,operator_inventory,assert_deterministic_pack,one_scale_zero_point,sha256_bytes
from m4_n1_compose import classifier_calibration_with_range_anchor
import m4_n1_se_mean_emit as se

sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,v):assert not p.exists();p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(v,indent=2,default=lambda x:np.asarray(x).tolist())+'\n')
def bankid(rows):return hashlib.sha256(''.join(f'{i:04d} {hashlib.sha256(np.ascontiguousarray(a).tobytes()).hexdigest()}\n' for i,a in enumerate(rows)).encode()).hexdigest()
def write(p,b):assert not p.exists();p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b);return dict(path=str(p.relative_to(R)),bytes=len(b),sha256=sha(p))
def pack_tensor(t):
 import flatbuffers
 b=flatbuffers.Builder(0);root=t.Pack(b);b.Finish(root);return bytes(b.Output())
def audit_delta(old,new,f):
 a=unpack_model(old);b=unpack_model(new);ga=a.subgraphs[0];gb=b.subgraphs[0]
 assert len(ga.tensors)==len(gb.tensors) and len(a.buffers)==len(b.buffers)
 changes=[]
 for i,(ta,tb) in enumerate(zip(ga.tensors,gb.tensors)):
  if pack_tensor(ta)==pack_tensor(tb):continue
  assert i==926,f'UNEXPECTED_TENSOR_CHANGE_{i}'
  sa,za=one_scale_zero_point(ta);sb,zb=one_scale_zero_point(tb)
  assert sa.tobytes()==struct.pack('<I',0x38420de1) and sb.tobytes()==struct.pack('<I',int(f['new_shared_feature_scale']['bits'],16)) and za==zb==0
  qa,qb=ta.quantization,tb.quantization
  delta=dict(tensor=i,name=ta.name.decode(),changed_fields=['quantization.scale','quantization.max'],old_scale=float(sa),new_scale=float(sb),old_scale_bits='0x38420de1',new_scale_bits=f['new_shared_feature_scale']['bits'],old_maximum=np.asarray(qa.max).tolist(),new_maximum=np.asarray(qb.max).tolist())
  normalized=copy.deepcopy(tb);normalized.quantization.scale=qa.scale;normalized.quantization.max=qa.max
  assert pack_tensor(normalized)==pack_tensor(ta),'UNRELATED_OUTPUT_TENSOR_METADATA_CHANGE'
  changes.append(delta)
 assert len(changes)==1
 for i,(ba,bb) in enumerate(zip(a.buffers,b.buffers)):
  xa=b'' if ba.data is None else np.asarray(ba.data,np.uint8).tobytes();xb=b'' if bb.data is None else np.asarray(bb.data,np.uint8).tobytes()
  assert xa==xb,f'UNEXPECTED_BUFFER_CHANGE_{i}'
 normalized=unpack_model(new);normalized.subgraphs[0].tensors[926]=a.subgraphs[0].tensors[926]
 assert pack_model(normalized)==pack_model(a),'UNRELATED_FLATBUFFER_OBJECT_CHANGE'
 return dict(result='PASS',changed_tensors=changes,changed_buffers=[],changed_derived_bias_scales=[],changed_derived_bias_codes=[],changed_learned_parameter_bytes=0,changed_operators=[],changed_operator_options=[],changed_graph_topology=False,unrelated_changes=0,normalization='Restore only output tensor 926; complete normalized object must equal exact V2 control')

def construct(execution):
 f=json.loads((O/'M4_V3_CANDIDATE_FREEZE.json').read_text());assert f['disposition']=='M4_RANGE_COVERING_V3_CANDIDATE_PROSPECTIVELY_FROZEN'
 for rel,h in f['implementation_sha256'].items():assert sha(R/rel)==h,rel
 for rel,h in f['retained_authority_sha256'].items():assert sha(R/rel)==h,rel
 d=O/f'candidate-v3-{execution}';assert not d.exists();tf=configure_tensorflow();runtime=assert_runtime_environment(tf)
 source,payload=source_binding(R/'.m3-work/canonical-h1-savedmodel',R/'.m1-work/official/BirdNET+_V3.0-preview3.1_Global_11K_Labels.csv');assert source['canonical_m3_saved_model']['tree_sha256']==f['M3_canonical_identity']['tree_sha256']
 module,report=slice_stages(payload,('shared_feature',));loaded=tf.saved_model.load(str(R/'.m3-work/canonical-h1-savedmodel'));components=make_components(loaded,module,report,tf);sources=save_components(components,d/'saved-model-components',tf)
 cf=json.loads((V2/'authority/CALIBRATION_FREEZE.json').read_text());waves=generated_waveforms(cf['seed'],cf['count']);assert bankid(waves)==cf['waveform_manifest']['aggregate_sha256']
 refmodule,refreport=slice_stages(payload,('shared_feature','embedding'));front=[];shared=[];emb=[]
 for i,w in enumerate(waves):
  x=frontend_value(loaded,w,tf);v=run_stage_slice(loaded,refmodule,refreport,x,tf);front.append(np.asarray(x.numpy(),np.float32));shared.append(np.asarray(v['shared_feature'],np.float32));emb.append(np.asarray(v['embedding'],np.float32))
  if i%16==0:print('V3_CALIBRATION',execution,i,flush=True)
 for field,rows in [('frontend_backbone_input',front),('shared_feature',shared),('gem_embedding',emb)]:assert bankid(rows)==cf['canonical_m3_boundary_aggregates'][field],field
 from tensorflow.lite.python.optimize import calibrator as cm
 original=cm.Calibrator;mode='v2';calibration_receipts=[]
 class ProspectiveCalibrator(original):
  def calibrate_and_quantize(self,dataset_gen,input_type,output_type,allow_float,activations_type=tf.int8,bias_type=tf.int32,resize_input=True,disable_per_channel=False,disable_per_channel_quantization_for_dense_layers=False):
   self._feed_tensors(dataset_gen,resize_input);calibrated=self._calibrator.Calibrate();assert sha256_bytes(calibrated)==f['origin_calibrated_model_sha256'],'CALIBRATED_BASELINE_IDENTITY_MISMATCH'
   m=unpack_model(calibrated);g=m.subgraphs[0];ti=g.tensors[int(g.inputs[0])];to=g.tensors[int(g.outputs[0])]
   assert float(ti.quantization.max[0])==7.0 and float(to.quantization.max[0])==1.5160022974014282
   ti.quantization.max=np.asarray([float32_from_bits(0x40e00001)],np.float32)
   if mode=='v3':to.quantization.max=np.asarray([float32_from_bits(int(f['output_calibration_maximum']['bits'],16))],np.float32)
   modified=pack_model(m);wrapper=original(modified)
   output=wrapper._calibrator.QuantizeModel(np.dtype(input_type.as_numpy_dtype()).num,np.dtype(output_type.as_numpy_dtype()).num,allow_float,np.dtype(activations_type.as_numpy_dtype()).num,np.dtype(bias_type.as_numpy_dtype()).num,disable_per_channel,disable_per_channel_quantization_for_dense_layers)
   calibration_receipts.append(dict(mode=mode,original_sha256=sha256_bytes(calibrated),modified_sha256=sha256_bytes(modified),input_maximum_bits='0x40e00001',output_maximum=float(to.quantization.max[0]),output_maximum_bits='0x'+np.asarray(to.quantization.max[0],dtype='<f4').tobytes()[::-1].hex()))
   return output
 cm.Calibrator=ProspectiveCalibrator
 try:
  control_raw=convert_component('backbone',d/'saved-model-components/backbone',front,tf);assert sha256_bytes(control_raw)==f['V2_raw_backbone_sha256'],'EXACT_V2_CONTROL_IDENTITY_MISMATCH'
  print('V2_COMPLETE_CONTROL_IDENTITY_PASS',execution,sha256_bytes(control_raw),flush=True)
  mode='v3';new_raw=convert_component('backbone',d/'saved-model-components/backbone',front,tf)
 finally:cm.Calibrator=original
 raw_delta=audit_delta(control_raw,new_raw,f)
 contract=json.loads((V2/'authority/CURRENT_SE_MEAN_CONTRACT.json').read_text());topology=json.loads((V2/'authority/SE_MEAN_TOPOLOGY_INVENTORY.json').read_text())
 def emit(raw):
  m=unpack_model(raw);matches=se.discover_canonical_matches(m,contract);se.assert_frozen_topology(matches,topology);proof=se.rewrite_matches_in_memory(m,matches);b=assert_deterministic_pack(m);assert operator_inventory(unpack_model(b))==topology['expected_emitted_operator_inventory'];return b,proof
 control,_=emit(control_raw);assert sha256_bytes(control)==f['V2_artifacts']['backbone']['sha256'],'EXACT_V2_EMISSION_IDENTITY_MISMATCH'
 backbone,proof=emit(new_raw);final_delta=audit_delta(control,backbone,f)
 frontend=convert_component('frontend',d/'saved-model-components/frontend',None,tf);classifier=convert_component('classifier',d/'saved-model-components/classifier',classifier_calibration_with_range_anchor(emb),tf)
 gem=(R/f['V2_artifacts']['gem']['path']).read_bytes();artifacts={}
 for name,data in [('frontend',frontend),('backbone',backbone),('gem',gem),('classifier',classifier)]:
  if name!='backbone':assert sha256_bytes(data)==f['V2_artifacts'][name]['sha256'],f'UNEXPECTED_COMPONENT_CHANGE_{name}'
  artifacts[name]=write(d/f'{name}.tflite',data)
 write(d/'backbone-raw-v2-control.tflite',control_raw);write(d/'backbone-raw-v3.tflite',new_raw)
 record=dict(state='EMITTED_UNQUALIFIED',candidate=f['candidate'],execution=execution,freeze_sha256=sha(O/'M4_V3_CANDIDATE_FREEZE.json'),source=source,runtime=runtime,component_sources=sources,calibration=dict(count=len(waves),waveforms=bankid(waves),frontend=bankid(front),shared_feature=bankid(shared),embedding=bankid(emb),source='FRESH_CANONICAL_M3_EXECUTION',candidate_inference_performed=False),calibration_changes=calibration_receipts,artifacts=artifacts,raw_v2_sha256=sha256_bytes(control_raw),raw_v3_sha256=sha256_bytes(new_raw),raw_delta=raw_delta,final_delta=final_delta,se_mean_proof=proof,candidate_inference_performed=False)
 save(d/'EMISSION.json',record);print('V3_EMITTED',execution,json.dumps(artifacts),flush=True)

if __name__=='__main__':construct(sys.argv[1])
