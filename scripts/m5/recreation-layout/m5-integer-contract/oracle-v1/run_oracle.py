#!/usr/bin/env python3
"""One non-formal frozen regression fixture; no U85/TFLite runtime invocation."""
from array import array
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
from model_io import write_package

BASE=Path(__file__).resolve().parent
PARENT=BASE.parent
ROOT=BASE.parents[2]
CONTRACT_SHA='0d3bcf5039ddba491f8902a582e8666fa62a6d924aa514df02a2543f5db7a8f2'

def ident(path):
    with path.open('rb') as f:h=hashlib.file_digest(f,'sha256').hexdigest()
    return {'path':str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path),'bytes':path.stat().st_size,'sha256':h}

def main():
    raw=(PARENT/'FULL_U85_INTEGER_ARITHMETIC_CONTRACT_V1.json').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=CONTRACT_SHA:raise ValueError('frozen contract changed')
    contract=json.loads(raw)
    tests=json.loads((PARENT/'PRIMITIVE_SELF_TEST_RESULT_V1.json').read_text())
    if tests['TOTAL_MISMATCHES'] or tests['MAXIMUM_LSB_ERROR']:raise ValueError('primitive prerequisite failed')
    frozen=json.loads((ROOT/'.development-work/m5-corstone-harness/SEALED_PAYLOAD_BINDING.json').read_text())
    run=BASE/'dry-canonical';run.mkdir(exist_ok=False)
    trace=[];boundaries={};started=time.monotonic()
    fixture=next(r for r in contract['future_38_exact_identities'] if r['fixture']=='canonical' and r['source_role']=='regression')
    for role in ['backbone','classifier']:
        input_id=frozen['components'][role]['frozen_input'];path=ROOT/input_id['path'];raw_input=path.read_bytes()
        if hashlib.sha256(raw_input).hexdigest()!=input_id['sha256']:raise ValueError('frozen dry input identity mismatch')
        package=run/(role+'.oracle-package');write_package(ROOT,contract,role,raw_input,package)
        out=run/role;out.mkdir()
        process=subprocess.Popen([str(BASE/'build/scalar_executor'),str(package),str(out)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        with (run/(role+'.execution.log')).open('x') as log:
            for line in process.stdout:
                log.write(line);log.flush();print(role+' '+line.strip(),flush=True)
        if process.wait()!=0:raise RuntimeError('scalar model execution failed')
        component=contract['components'][role]
        operators=[o for o in contract['operators'] if o['component']==role]
        for op in operators:
            tensor_id=op['outputs'][0];metadata=component['tensors'][tensor_id]
            path=out/f'op{op["operator_index"]}-tensor{tensor_id}.i16le';codes=array('h');codes.frombytes(path.read_bytes())
            if sys.byteorder!='little':codes.byteswap()
            count=1
            for d in metadata['shape']:count*=d
            if len(codes)!=count:raise ValueError('operator output completeness/shape failure')
            trace.append({'component':role,'operator_index':op['operator_index'],'type':op['opcode_type'],'output_tensor_id':tensor_id,
               'dtype':'INT16','shape':metadata['shape'],'elements':len(codes),'min':min(codes),'max':max(codes),**ident(path)})
        boundary_name='deployment_input' if role=='backbone' else 'classifier_input'
        meta=component['tensors'][component['inputs'][0]]
        codes=array('h');codes.frombytes(raw_input)
        if sys.byteorder!='little':codes.byteswap()
        boundaries[boundary_name]={'status':'PRODUCED_FROM_EXACT_FROZEN_STAGE_INPUT','shape':meta['shape'],'dtype':'INT16','min':min(codes),'max':max(codes),**ident(ROOT/input_id['path'])}
        final=trace[-1];name='shared_feature' if role=='backbone' else 'integer_logits'
        boundaries[name]=final|{'status':'PRODUCED_BY_INDEPENDENT_FULL_COMPONENT_EXECUTION'}
    if len(trace)!=550 or len(boundaries)!=4:raise ValueError('full execution completeness failure')
    trace_file=run/'ALL_550_OPERATOR_OUTPUT_DIGESTS_V1.json'
    trace_file.write_text(json.dumps({'operator_count':550,'ordered_outputs':trace},indent=2)+'\n')
    result={'schema':'m5-independent-scalar-oracle-non-formal-dry-validation-v1','disposition':'ORACLE_550_OP_DRY_EXECUTION_PASS',
        'recorded_at_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'elapsed_seconds':time.monotonic()-started,
        'fixture':'canonical','fixture_role':'one_frozen_regression','waveform_input_identity':fixture,
        'contract_sha256':CONTRACT_SHA,'primitive_self_test_receipt':ident(PARENT/'PRIMITIVE_SELF_TEST_RESULT_V1.json'),
        'operator_count':550,'backbone_operators_executed':544,'classifier_operators_executed':6,'missing_operators':0,
        'four_boundaries':boundaries,'internal_trace':ident(trace_file),
        'trace_contains_all_550_shapes_ranges_and_output_hashes':True,
        'component_inputs':'Two separately frozen component stage inputs, matching qualified Corstone route scope. FP32 frontend/GeM pipeline not invoked.',
        'composed_GeM_pipeline_executed':False,'Corstone_U85_execution':False,'oracle_U85_comparison':False,
        'formal_38_fixture_campaign_executed':False,'formal_fixture_comparisons':0,'contract_oracle_tuning_from_outputs':False}
    (PARENT/'ORACLE_DRY_EXECUTION_V1.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'disposition':result['disposition'],'operator_count':550,'boundary_hashes':{k:v['sha256'] for k,v in boundaries.items()},'trace_sha256':ident(trace_file)['sha256'],'elapsed_seconds':result['elapsed_seconds']}))

if __name__=='__main__':main()
