/* Sealed bytes -> retained driver -> U85. No CPU model kernels or bridge math. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "ethosu_driver.h"
#include "ethosu_interface_u85.h"
#include "bindings.h"
#include "sha256.h"
#include "semihost.h"
#include "input_catalog.h"
#define ARENA ((void*)0x70010000u)
#define FAST ((void*)0x31000000u)
#define BB_OUTPUT ((void*)((uintptr_t)ARENA+BB_OUTPUT_OFFSET))
#define CL_OUTPUT ((void*)((uintptr_t)ARENA+CL_OUTPUT_OFFSET))
#define BB_OUTPUT_BYTES 161280u
#define CL_OUTPUT_BYTES 23120u
extern const uint8_t sealed_backbone[],sealed_backbone_end[],sealed_classifier[],sealed_classifier_end[];
extern uint8_t fixture_backbone[],fixture_classifier[];
static const struct input_identity *selected;
static unsigned loader_only;
static char source_bb_sha[65],source_cl_sha[65];
static struct ethosu_driver drv;
static volatile unsigned irq_count,begin_count,end_count;
static volatile uint32_t irq_status,irq_qread,irq_qsize,irq_qbase,irq_regioncfg;
static volatile uint32_t completed_status,completed_qread,completed_qsize;
static volatile struct NPU_REG *const regs=(volatile struct NPU_REG*)0x50004000u;
void NPU0_Handler(void){
 irq_count++;irq_status=regs->STATUS.word;irq_qread=regs->QREAD.word;irq_qsize=regs->QSIZE.word;irq_qbase=regs->QBASE.word[0];irq_regioncfg=regs->REGIONCFG.word;
 ethosu_irq_handler(&drv);
}
void ethosu_flush_dcache(const uint64_t *a,const size_t *n,int count){(void)a;(void)n;(void)count;__asm volatile("dsb sy":::"memory");}
void ethosu_invalidate_dcache(const uint64_t *a,const size_t *n,int count){(void)a;(void)n;(void)count;__asm volatile("dsb sy":::"memory");}
void ethosu_inference_begin(struct ethosu_driver *d,void *arg){(void)d;(void)arg;begin_count++;}
void ethosu_inference_end(struct ethosu_driver *d,void *arg){(void)d;(void)arg;end_count++;completed_status=regs->STATUS.word;completed_qread=regs->QREAD.word;completed_qsize=regs->QSIZE.word;}
static void verify(const char *label,const void *data,size_t bytes,const char *expected){
 char h[65];sha256_hex(data,bytes,h);printf("SHA256 %s bytes=%u hash=%s\n",label,(unsigned)bytes,h);
 if(strcmp(h,expected)){printf("IDENTITY_FAILURE %s\n",label);diagnostic_exit(10);}
 printf("IDENTITY_PASS %s\n",label);
}
static void output(const char *label,const void *data,size_t bytes){char h[65];sha256_hex(data,bytes,h);printf("OUTPUT %s bytes=%u dtype=INT16 hash=%s\n",label,(unsigned)bytes,h);diagnostic_dump(label,data,bytes);}
static void acquire_inputs(void){
 diagnostic_read_exact(selected->bb_path,fixture_backbone,BB_INPUT_BYTES);
 sha256_hex(fixture_backbone,BB_INPUT_BYTES,source_bb_sha);
 verify("BACKBONE_INPUT",fixture_backbone,BB_INPUT_BYTES,selected->bb_sha);
 diagnostic_read_exact(selected->cl_path,fixture_classifier,CL_INPUT_BYTES);
 sha256_hex(fixture_classifier,CL_INPUT_BYTES,source_cl_sha);
 verify("CLASSIFIER_INPUT",fixture_classifier,CL_INPUT_BYTES,selected->cl_sha);
}
static int invoke(const char *name,const uint8_t *model,unsigned payload_offset,unsigned payload_bytes,unsigned constants_offset,unsigned constants_bytes,unsigned arena_bytes,unsigned fast_bytes,const uint8_t *frozen_input,unsigned input_offset,unsigned input_bytes,void *out,unsigned out_bytes,unsigned command_bytes){
 memset(ARENA,0,arena_bytes);memset(FAST,0,524288);memset(out,0x5a,out_bytes);
 uint8_t *input=(uint8_t*)ARENA+input_offset;
 if(input_offset+input_bytes>arena_bytes||(uintptr_t)out+out_bytes>(uintptr_t)ARENA+arena_bytes){printf("OFFLINE_ALLOCATION_BOUNDS_FAILURE\n");return 22;}
 memcpy(input,frozen_input,input_bytes);
 const int is_backbone=!strcmp(name,"backbone");
 verify(is_backbone?"BACKBONE_RESIDENT_INPUT":"CLASSIFIER_RESIDENT_INPUT",input,input_bytes,is_backbone?selected->bb_sha:selected->cl_sha);
 char resident_sha[65];sha256_hex(input,input_bytes,resident_sha);
 printf("INPUT_TRANSPORT_PASS fixture=%s component=%s expected_bytes=%u source_sha=%s expected_sha=%s resident_address=%08lx resident_sha=%s equality=1\n",selected->id,name,input_bytes,is_backbone?source_bb_sha:source_cl_sha,is_backbone?selected->bb_sha:selected->cl_sha,(unsigned long)(uintptr_t)input,resident_sha);
 if(loader_only){char path[128];snprintf(path,sizeof(path),"%s-%s.resident.i16le",selected->id,name);diagnostic_dump(path,input,input_bytes);return 0;}
 diagnostic_dump(is_backbone?"backbone_input.i16le":"classifier_input.i16le",input,input_bytes);
 char output_before[65];sha256_hex(out,out_bytes,output_before);
 uint64_t bases[5]={(uintptr_t)(model+constants_offset),(uintptr_t)ARENA,(uintptr_t)FAST,(uintptr_t)input,(uintptr_t)out};
 size_t sizes[5]={constants_bytes,arena_bytes,fast_bytes,input_bytes,out_bytes};
 unsigned irq_before=irq_count,begin_before=begin_count,end_before=end_count;
 printf("INVOKE_BEGIN %s COP1=%08lx payload_bytes=%u command_bytes=%u\n",name,(unsigned long)(uintptr_t)(model+payload_offset),payload_bytes,command_bytes);
 for(unsigned i=0;i<5;i++)printf("BASEP %s index=%u address=%08lx bytes=%u\n",name,i,(unsigned long)bases[i],(unsigned)sizes[i]);
 int rc=ethosu_invoke_v3(&drv,model+payload_offset,(int)payload_bytes,bases,sizes,5,(void*)name);
 printf("INVOKE_END %s rc=%d IRQ_DELTA=%u BEGIN_DELTA=%u END_DELTA=%u IRQ_STATUS=%08lx IRQ_QREAD=%lu IRQ_QSIZE=%lu IRQ_QBASE=%08lx REGIONCFG=%08lx FINAL_STATUS=%08lx FINAL_QREAD=%lu FINAL_QSIZE=%lu\n",name,rc,irq_count-irq_before,begin_count-begin_before,end_count-end_before,(unsigned long)irq_status,(unsigned long)irq_qread,(unsigned long)irq_qsize,(unsigned long)irq_qbase,(unsigned long)irq_regioncfg,(unsigned long)completed_status,(unsigned long)completed_qread,(unsigned long)completed_qsize);
 if(rc||irq_count-irq_before!=1||begin_count-begin_before!=1||end_count-end_before!=1||irq_qread!=command_bytes||irq_qsize!=command_bytes){printf("U85_COMPLETION_FAILURE %s\n",name);return 20;}
 char output_after[65];sha256_hex(out,out_bytes,output_after);
 printf("OUTPUT_WRITE_EVIDENCE %s before=%s after=%s changed=%u\n",name,output_before,output_after,(unsigned)(strcmp(output_before,output_after)!=0));
 if(!strcmp(output_before,output_after)){printf("OUTPUT_UNCHANGED_FAILURE %s\n",name);return 23;}
 if(!((struct status_r){.word=irq_status}).cmd_end_reached){printf("U85_COMMAND_END_FAILURE\n");return 21;}
 return 0;
}
static int loader_pair(unsigned index){
 selected=&input_catalog[index];acquire_inputs();
 int rc=invoke("backbone",sealed_backbone,BB_PAYLOAD_OFFSET,BB_PAYLOAD_BYTES,BB_CONSTANTS_OFFSET,BB_CONSTANTS_BYTES,BB_ARENA_BYTES,BB_FAST_BYTES,fixture_backbone,BB_INPUT_OFFSET,BB_INPUT_BYTES,BB_OUTPUT,BB_OUTPUT_BYTES,167836);if(rc)return rc;
 return invoke("classifier",sealed_classifier,CL_PAYLOAD_OFFSET,CL_PAYLOAD_BYTES,CL_CONSTANTS_OFFSET,CL_CONSTANTS_BYTES,CL_ARENA_BYTES,CL_FAST_BYTES,fixture_classifier,CL_INPUT_OFFSET,CL_INPUT_BYTES,CL_OUTPUT,CL_OUTPUT_BYTES,1040);
}
int main(void){
 setvbuf(stdout,NULL,_IONBF,0);setvbuf(stderr,NULL,_IONBF,0);

 verify("SHA256_SELFTEST_ABC","abc",3,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
 /* Request selects identity and mode, never supplies expected hashes or sizes. */
 uint32_t request[4];diagnostic_read_exact("route_request.bin",request,sizeof(request));
 if(request[0]!=0x31524649u||request[1]>2||request[2]>=INPUT_CATALOG_COUNT||request[3]){printf("INPUT_REQUEST_FAILURE\n");return 84;}
 selected=&input_catalog[request[2]];loader_only=request[1]!=1;
 printf("BIRDNET_M5_CORSTONE_DIAGNOSTIC_BOOT CPU=Cortex-M85 CCR=%08lx FIXTURE=%s MODE=%u\n",*(volatile unsigned long*)0xe000ed14,selected->id,(unsigned)request[1]);
 if(loader_only){
  unsigned first=request[1]==0?0:request[2],limit=request[1]==0?INPUT_CATALOG_COUNT:first+1;
  for(unsigned i=first;i<limit;i++){int rc=loader_pair(i);if(rc)return rc;}
  printf("INPUT_ONLY_VALIDATION_PASS pairs=%u inputs=%u U85_SUBMISSIONS=0 IRQ_COUNT=%u\n",limit-first,2*(limit-first),irq_count);return 0;
 }
 acquire_inputs();
 if((size_t)(sealed_backbone_end-sealed_backbone)!=BB_MODEL_BYTES||(size_t)(sealed_classifier_end-sealed_classifier)!=CL_MODEL_BYTES){printf("EMBEDDED_LENGTH_FAILURE\n");return 11;}
 verify("SEALED_BACKBONE_CONTAINER",sealed_backbone,BB_MODEL_BYTES,BB_MODEL_SHA256);
 verify("SEALED_CLASSIFIER_CONTAINER",sealed_classifier,CL_MODEL_BYTES,CL_MODEL_SHA256);
 verify("BACKBONE_COP1",sealed_backbone+BB_PAYLOAD_OFFSET,BB_PAYLOAD_BYTES,BB_PAYLOAD_SHA256);
 verify("CLASSIFIER_COP1",sealed_classifier+CL_PAYLOAD_OFFSET,CL_PAYLOAD_BYTES,CL_PAYLOAD_SHA256);
 verify("BACKBONE_CONSTANTS",sealed_backbone+BB_CONSTANTS_OFFSET,BB_CONSTANTS_BYTES,BB_CONSTANTS_SHA256);
 verify("CLASSIFIER_CONSTANTS",sealed_classifier+CL_CONSTANTS_OFFSET,CL_CONSTANTS_BYTES,CL_CONSTANTS_SHA256);
 printf("U85_REGISTERS_BEFORE_INIT ID=%08lx CONFIG=%08lx\n",(unsigned long)regs->ID.word,(unsigned long)regs->CONFIG.word);
 int rc=ethosu_init(&drv,(void*)regs,FAST,524288,1,1);
 printf("U85_INIT rc=%d\n",rc);if(rc)return 12;
 struct ethosu_hw_info hw;ethosu_get_hw_info(&drv,&hw);
 printf("U85_IDENTIFIED product=%u arch=%u.%u.%u MACs=%u CMD_VERSION=%u ID=%08lx CONFIG=%08lx\n",(unsigned)regs->CONFIG.product,(unsigned)hw.version.arch_major_rev,(unsigned)hw.version.arch_minor_rev,(unsigned)hw.version.arch_patch_rev,1u<<hw.cfg.macs_per_cc,(unsigned)hw.cfg.cmd_stream_version,(unsigned long)regs->ID.word,(unsigned long)regs->CONFIG.word);
 if(regs->CONFIG.product!=2||hw.cfg.macs_per_cc!=8||hw.cfg.cmd_stream_version!=1){printf("U85_CONFIGURATION_FAILURE\n");return 13;}
 *(volatile uint32_t*)0xe000e100=1u<<16;__asm volatile("cpsie i":::"memory");
 rc=invoke("backbone",sealed_backbone,BB_PAYLOAD_OFFSET,BB_PAYLOAD_BYTES,BB_CONSTANTS_OFFSET,BB_CONSTANTS_BYTES,BB_ARENA_BYTES,BB_FAST_BYTES,fixture_backbone,BB_INPUT_OFFSET,BB_INPUT_BYTES,BB_OUTPUT,BB_OUTPUT_BYTES,167836);if(rc)return rc;
 output("shared_feature.i16le",BB_OUTPUT,BB_OUTPUT_BYTES);diagnostic_dump("arena_after_backbone.bin",ARENA,BB_ARENA_BYTES);diagnostic_dump("fast_after_backbone.bin",FAST,BB_FAST_BYTES);
 verify("FROZEN_BACKBONE_INPUT_AFTER",fixture_backbone,BB_INPUT_BYTES,selected->bb_sha);
 printf("BOUNDARY shared_feature shape=1,7,9,1280 source_tensor=926 scale_bits=0x38606d52\n");
 rc=invoke("classifier",sealed_classifier,CL_PAYLOAD_OFFSET,CL_PAYLOAD_BYTES,CL_CONSTANTS_OFFSET,CL_CONSTANTS_BYTES,CL_ARENA_BYTES,CL_FAST_BYTES,fixture_classifier,CL_INPUT_OFFSET,CL_INPUT_BYTES,CL_OUTPUT,CL_OUTPUT_BYTES,1040);if(rc)return rc;
 output("logits.i16le",CL_OUTPUT,CL_OUTPUT_BYTES);diagnostic_dump("arena_after_classifier.bin",ARENA,CL_ARENA_BYTES);diagnostic_dump("fast_after_classifier.bin",FAST,CL_FAST_BYTES);
 verify("FROZEN_CLASSIFIER_INPUT_AFTER",fixture_classifier,CL_INPUT_BYTES,selected->cl_sha);
 printf("BOUNDARY logits shape=1,11560 source_tensor=9 scale_bits=0x3a08d017\n");
 printf("ROUTE_SMOKE_PASS U85_COMPONENTS=2 IRQ_COUNT=%u NO_CPU_FALLBACK=1 NO_MODEL_RECONSTRUCTION=1\n",irq_count);
 return 0;
}
