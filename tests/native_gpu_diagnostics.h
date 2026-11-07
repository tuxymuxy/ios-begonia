/* SPDX-License-Identifier: MIT
 * Crash-only diagnostic: capture the actual instruction and ICD-relative PC.
 * No recovery or instruction emulation is attempted in the signal handler.
 */
#ifndef BEGONIA_NATIVE_GPU_DIAGNOSTICS_H
#define BEGONIA_NATIVE_GPU_DIAGNOSTICS_H
#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ucontext.h>
#include <unistd.h>
static uintptr_t diagnostic_icd_base;
static void native_gpu_crash(int number,siginfo_t *info,void *context) {
 ucontext_t *state=context;uintptr_t pc=state->uc_mcontext->__ss.__pc;
 uint32_t instruction=number==SIGILL && !(pc&3)?*(const volatile uint32_t *)pc:0;
 char line[256];int size=snprintf(line,sizeof line,"BEGONIA GPU TRAP: signal=%d code=%d pc=%llx icd_base=%llx offset=%llx instruction=%08x\n",number,info->si_code,(unsigned long long)pc,(unsigned long long)diagnostic_icd_base,(unsigned long long)(pc-diagnostic_icd_base),instruction);
 if(size>0)(void)write(STDERR_FILENO,line,(size_t)size<sizeof line?(size_t)size:sizeof line-1);
 _exit(number==SIGALRM?124:128+number);
}
static void native_gpu_install_crash_diagnostics(void) {
 struct sigaction action={0};action.sa_sigaction=native_gpu_crash;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);
 const int signals[]={SIGILL,SIGBUS,SIGSEGV,SIGABRT,SIGALRM};
 for(unsigned i=0;i<sizeof signals/sizeof signals[0];i++)sigaction(signals[i],&action,NULL);
}
static void __attribute__((unused)) native_gpu_record_icd(void *entry) {
 Dl_info info={0};if(entry && dladdr(entry,&info))diagnostic_icd_base=(uintptr_t)info.dli_fbase;
}
#endif
