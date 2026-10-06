/* Private minimal secure Cortex-M85 startup for the retained Corstone-320 FVP. */
#include <stdint.h>
#include <stdio.h>
#include "semihost.h"
extern uint32_t __stack_top[],__bss_start[],__bss_end[];
int main(void);
void NPU0_Handler(void);
void Reset_Handler(void);
void Fault_Handler(void){
 printf("TARGET_EXCEPTION CFSR=%08lx HFSR=%08lx BFAR=%08lx MMFAR=%08lx\n",*(volatile unsigned long*)0xe000ed28,*(volatile unsigned long*)0xe000ed2c,*(volatile unsigned long*)0xe000ed38,*(volatile unsigned long*)0xe000ed34);diagnostic_exit(100);
}
__attribute__((section(".vectors"),used,aligned(512)))
const uintptr_t vectors[496]={
 [0]=(uintptr_t)__stack_top,[1]=(uintptr_t)Reset_Handler,[2]=(uintptr_t)Fault_Handler,[3]=(uintptr_t)Fault_Handler,[4]=(uintptr_t)Fault_Handler,[5]=(uintptr_t)Fault_Handler,[6]=(uintptr_t)Fault_Handler,[7]=(uintptr_t)Fault_Handler,[11]=(uintptr_t)Fault_Handler,[12]=(uintptr_t)Fault_Handler,[14]=(uintptr_t)Fault_Handler,[15]=(uintptr_t)Fault_Handler,[32]=(uintptr_t)NPU0_Handler
};
void Reset_Handler(void){
 *(volatile uint32_t*)0xe000ed08=(uint32_t)(uintptr_t)vectors;
 for(uint32_t *p=__bss_start;p<__bss_end;p++)*p=0;
 *(volatile uint32_t*)0xe000ed14&=~((1u<<16)|(1u<<17));
 __asm volatile("dsb sy\n isb sy":::"memory");
 diagnostic_exit(main());
}
