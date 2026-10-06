#!/usr/bin/env python3
"""Only the immutable mathematical primitive corpus supplies expectations."""
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from arithmetic import evaluate_case
from model_io import scalar_case_line

BASE=Path(__file__).resolve().parent
PARENT=BASE.parent
CORPUS_SHA='bc90f90172922d6bfd2fe6ae34afd2c927fe7e8a9093dd19fba5108cda878134'

def main():
    raw=(PARENT/'PRIMITIVE_CORPUS_V1.json').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=CORPUS_SHA:raise ValueError('frozen primitive corpus changed')
    corpus=json.loads(raw);cases=corpus['cases'];lut=corpus['sigmoid_lut']
    work=BASE/'primitive-tests';work.mkdir(exist_ok=True)
    lines='\n'.join(scalar_case_line(c) for c in cases)+'\n'
    # This input excludes all expected values; the executor only receives arguments.
    (work/'arguments-only.txt').write_text(lines)
    lutfile=work/'sealed-lut-pairs.txt';lutfile.write_text('\n'.join(f'{a} {b}' for a,b in lut)+'\n')
    p=subprocess.run([str(BASE/'build/scalar_executor'),'--primitives',str(lutfile)],input=lines,text=True,capture_output=True,check=True)
    (work/'scalar-results.txt').write_text(p.stdout)
    rows=p.stdout.splitlines()
    if len(rows)!=len(cases):raise ValueError('primitive output completeness failure')
    counts={};mismatches=[];maximum=0
    for case,line in zip(cases,rows):
        cols=line.split();got=[int(v) for v in cols[2:]]
        if cols[0]!=case['id'] or int(cols[1])!=len(got):raise ValueError('primitive result identity mismatch')
        expected=case['expected'];want=expected if isinstance(expected,list) else [expected]
        py=evaluate_case(case,lut);py=py if isinstance(py,list) else [py]
        for implementation,result in [('python_scalar',py),('cpp_scalar',got)]:
            if len(result)!=len(want):raise ValueError('primitive result shape mismatch')
            for k,(a,b) in enumerate(zip(result,want)):
                error=abs(a-b);maximum=max(maximum,error)
                if error:mismatches.append({'id':case['id'],'implementation':implementation,'element':k,'expected':b,'observed':a})
        counts[case['primitive']]=counts.get(case['primitive'],0)+1
    result={'schema':'m5-oracle-frozen-primitive-self-tests-v1','recorded_at_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
       'disposition':'PRIMITIVE_SELF_TESTS_PASS' if not mismatches else 'IMPLEMENTATION_FAILURE',
       'primitive_corpus_sha256':CORPUS_SHA,'case_count':len(cases),'implementations_tested':['independent_python_scalar','independent_cpp_scalar_used_for_full_graph'],
       'case_count_per_implementation':len(cases),'total_implementation_case_checks':2*len(cases),
       'primitive_counts':counts,'TOTAL_MISMATCHES':len(mismatches),'MAXIMUM_LSB_ERROR':maximum,
       'mismatches':mismatches,'expected_values_from_runtime':False,'contract_changed':False,'formal_campaign_executed':False}
    name='PRIMITIVE_SELF_TEST_RESULT_V1.json' if not mismatches else 'PRIMITIVE_SELF_TEST_IMPLEMENTATION_FAILURE.json'
    (PARENT/name).write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ['mismatches','primitive_counts']}))
    if mismatches:raise SystemExit(1)

if __name__=='__main__':main()
