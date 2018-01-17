#include <assert.h>

#include <chrono>
#include <thread>

#include "amxexecutor.h"
#include "amxopcode.h"
#include "log.h"

namespace {
#if !defined NDEBUG
  static int check_endian(void)
  {
    uint16_t val=0x00ff;
    unsigned char *ptr=(unsigned char *)&val;
    /* "ptr" points to the starting address of "val". If that address
     * holds the byte "0xff", the computer stored the low byte of "val"
     * at the lower address, and so the memory lay out is Little Endian.
     */
    assert(*ptr==0xff || *ptr==0x00);
    #if BYTE_ORDER==BIG_ENDIAN
      return *ptr==0x00;  /* return "true" if big endian */
    #else
      return *ptr==0xff;  /* return "true" if little endian */
    #endif
  }
#endif

#if !defined assert_static
  /* see "Compile-Time Assertions" by Ralf Holly,
   * C/C++ Users Journal, November 2004
   */
  #define assert_static(e) \
    do { \
      enum { assert_static__ = 1/(e) }; \
    } while (0)
#endif
}

AMXExecutor::AMXExecutor(AMX *amx) : AMXService<AMXExecutor>(amx)
{}

#if -8/3==-2 && 8/-3==-2
  #define TRUNC_SDIV    /* signed divisions are truncated on this platform */
#else
  #define IABS(a)       ((a)>=0 ? (a) : (-a))
#endif

#if !defined _R
  #define _R_DEFAULT            /* mark default memory access */
  #define _R(base,addr)         (* (cell *)((unsigned char*)(base)+(int)(addr)))
  #define _R8(base,addr)        (* (unsigned char *)((unsigned char*)(base)+(int)(addr)))
  #define _R16(base,addr)       (* (uint16_t *)((unsigned char*)(base)+(int)(addr)))
  #define _R32(base,addr)       (* (uint32_t *)((unsigned char*)(base)+(int)(addr)))
#endif
#if !defined _W
  #define _W_DEFAULT            /* mark default memory access */
  #define _W(base,addr,value)   ((*(cell *)((unsigned char*)(base)+(int)(addr)))=(cell)(value))
  #define _W8(base,addr,value)  ((*(unsigned char *)((unsigned char*)(base)+(int)(addr)))=(unsigned char)(value))
  #define _W16(base,addr,value) ((*(uint16_t *)((unsigned char*)(base)+(int)(addr)))=(uint16_t)(value))
  #define _W32(base,addr,value) ((*(uint32_t *)((unsigned char*)(base)+(int)(addr)))=(uint32_t)(value))
#endif

#define NUMENTRIES(hdr,field,nextfield) \
                        (unsigned)(((hdr)->nextfield - (hdr)->field) / (hdr)->defsize)
#define GETENTRY(hdr,table,index) \
                        (AMX_FUNCSTUB *)((unsigned char*)(hdr) + (unsigned)(hdr)->table + (unsigned)index*(hdr)->defsize)

#if !defined _RCODE
  #define _RCODE()      ( *cip++ )
#endif

#if !defined GETPARAM
  #define GETPARAM(v)   ( v=_RCODE() )   /* read a parameter from the opcode stream */
#endif
#define SKIPPARAM(n)    ( cip=(cell *)cip+(n) ) /* for obsolete opcodes */

#define PUSH(v)         ( stk-=sizeof(cell), _W(data,stk,v) )
#define POP(v)          ( v=_R(data,stk), stk+=sizeof(cell) )

#define ABORT(amx,v)    { (amx)->stk=reset_stk; (amx)->hea=reset_hea; return v; }

#define CHKMARGIN()     if (hea+STKMARGIN>stk) return AMX_ERR_STACKERR
#define CHKSTACK()      if (stk>_amx->stp) return AMX_ERR_STACKLOW
#define CHKHEAP()       if (hea<_amx->hlw) return AMX_ERR_HEAPLOW

#define STKMARGIN       ((cell)(16*sizeof(cell)))

#if PAWN_CELL_SIZE==16 || defined AMX_DONT_RELOCATE
  #define JUMPABS(base,ip)      ((cell *)((base) + *(ip)))
  #define RELOC_ABS(base, off)
  #define RELOC_VALUE(base, v)
#else
  #define JUMPABS(base, ip)     ((cell *)*(ip))
  #define RELOC_ABS(base, off)  (*(ucell *)((base)+(int)(off)) += (ucell)(base))
  #define RELOC_VALUE(base, v)  ((v)+((ucell)(base)))
#endif

int AMXExecutor::HandleAMXExec(cell *retval, int index) {
  AMX *_amx = amx();
  AMX_HEADER *hdr;
  AMX_FUNCSTUB *func;
  unsigned char *code, *data;
  cell pri,alt,stk,frm,hea;
  cell reset_stk, reset_hea, *cip;
  ucell codesize;
  int i;
  #if defined ASM32 || defined JIT
    cell  parms[9];     /* registers and parameters for assembler _amx */
  #else
    AMXOpcode op;
    cell offs,val;
    int num;
  #endif
  #if defined ASM32
    extern void const *amx_opcodelist[];
    #ifdef __WATCOMC__
      #pragma aux amx_opcodelist "_*"
    #endif
  #endif
  #if defined JIT
    extern void const *amx_opcodelist_jit[];
    #ifdef __WATCOMC__
      #pragma aux amx_opcodelist_jit "_*"
    #endif
  #endif

  assert(_amx!=NULL);
  #if defined ASM32 || defined JIT
    /* HACK: return label table (for amx_BrowseRelocate) if _amx structure
     * is not passed.
     */
    if ((_amx->flags & AMX_FLAG_BROWSE)==AMX_FLAG_BROWSE) {
      assert(sizeof(cell)==sizeof(void *));
      assert(retval!=NULL);
      #if defined ASM32 && defined JIT
        if ((_amx->flags & AMX_FLAG_JITC)!=0)
          *retval=(cell)amx_opcodelist_jit;
        else
          *retval=(cell)amx_opcodelist;
      #elif defined ASM32
        *retval=(cell)amx_opcodelist;
      #else
        *retval=(cell)amx_opcodelist_jit;
      #endif
      return 0;
    } /* if */
  #endif

  if (_amx->callback==NULL)
    return AMX_ERR_CALLBACK;
  if ((_amx->flags & AMX_FLAG_RELOC)==0)
    return AMX_ERR_INIT;
  if ((_amx->flags & AMX_FLAG_NTVREG)==0) {
    if ((i=amx_Register(_amx,NULL,0))!=AMX_ERR_NONE)
      return i;
  } /* if */
  assert((_amx->flags & AMX_FLAG_BROWSE)==0);

  /* set up the registers */
  hdr=(AMX_HEADER *)_amx->base;
  assert(hdr->magic==AMX_MAGIC);
  codesize=(ucell)(hdr->dat-hdr->cod);
  code=_amx->base+(int)hdr->cod;
  data=(_amx->data!=NULL) ? _amx->data : _amx->base+(int)hdr->dat;
  hea=_amx->hea;
  stk=_amx->stk;
  reset_stk=stk;
  reset_hea=hea;
  alt=frm=pri=0;/* just to avoid compiler warnings */

  /* get the start address */
  if (index==AMX_EXEC_MAIN) {
    if (hdr->cip<0)
      return AMX_ERR_INDEX;
    cip=(cell *)(code + (int)hdr->cip);
  } else if (index==AMX_EXEC_CONT) {
    /* all registers: pri, alt, frm, cip, hea, stk, reset_stk, reset_hea */
    frm=_amx->frm;
    stk=_amx->stk;
    hea=_amx->hea;
    pri=_amx->pri;
    alt=_amx->alt;
    reset_stk=_amx->reset_stk;
    reset_hea=_amx->reset_hea;
    cip=(cell *)(code + (int)_amx->cip);
  } else if (index<0) {
    return AMX_ERR_INDEX;
  } else {
    if (index>=(cell)NUMENTRIES(hdr,publics,natives))
      return AMX_ERR_INDEX;
    func=GETENTRY(hdr,publics,index);
    cip=(cell *)(code + (int)func->address);
  } /* if */
  /* check values just copied */
  CHKSTACK();
  CHKHEAP();
  assert(check_endian());

  /* sanity checks */
  assert_static(AMX_OP_PUSH_PRI==36);
  assert_static(AMX_OP_PROC==46);
  assert_static(AMX_OP_SHL==65);
  assert_static(AMX_OP_SMUL==72);
  assert_static(AMX_OP_EQ==95);
  assert_static(AMX_OP_INC_PRI==107);
  assert_static(AMX_OP_MOVS==117);
  assert_static(AMX_OP_SYMBOL==126);
  assert_static(AMX_OP_PUSH2_C==138);
  assert_static(AMX_OP_LOAD_BOTH==154);
  #if PAWN_CELL_SIZE==16
    assert_static(sizeof(cell)==2);
  #elif PAWN_CELL_SIZE==32
    assert_static(sizeof(cell)==4);
  #elif PAWN_CELL_SIZE==64
    assert_static(sizeof(cell)==8);
  #else
    #error Unsupported cell size
  #endif

  if (index!=AMX_EXEC_CONT) {
    reset_stk+=_amx->paramcount*sizeof(cell);
    PUSH(_amx->paramcount*sizeof(cell));
    _amx->paramcount=0;          /* push the parameter count to the stack & reset */
    #if defined ASM32 || defined JIT
      PUSH(RELOC_VALUE(code,0));/* relocated zero return address */
    #else
      PUSH(0);                  /* zero return address */
    #endif
  } /* if */
  /* check stack/heap before starting to run */
  CHKMARGIN();

  /* start running */
#if defined ASM32 || defined JIT
  /* either the assembler abstract machine or the JIT; both by Marc Peter */

  parms[0] = pri;
  parms[1] = alt;
  parms[2] = (cell)cip;
  parms[3] = (cell)data;
  parms[4] = stk;
  parms[5] = frm;
  parms[6] = (cell)_amx;
  parms[7] = (cell)code;
  parms[8] = (cell)codesize;

  #if defined ASM32 && defined JIT
    if ((_amx->flags & AMX_FLAG_JITC)!=0)
      i = amx_exec_jit(parms,retval,_amx->stp,hea);
    else
      i = amx_exec_asm(parms,retval,_amx->stp,hea);
  #elif defined ASM32
    i = amx_exec_asm(parms,retval,_amx->stp,hea);
  #else
    i = amx_exec_jit(parms,retval,_amx->stp,hea);
  #endif
  if (i == AMX_ERR_SLEEP) {
    _amx->reset_stk=reset_stk;
    _amx->reset_hea=reset_hea;
  } else {
    /* remove parameters from the stack; do this the "hard" way, because
     * the assembler version has no internal knowledge of the local
     * variables, so any "clean" way would be a kludge anyway.
     */
    _amx->stk=reset_stk;
    _amx->hea=reset_hea;
  } /* if */
  return i;

#else

  for ( ;; ) {
    LogDebugPrint("Current CIP: %d | Current OP: %s | Param or next OP: %d",
      (uint32_t) cip,
      std::string(AMXOpcodeNames[*cip]).c_str(),
      *(cip + 1)
    );
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    op=(AMXOpcode) _RCODE();
    switch (op) {
    case AMX_OP_LOAD_PRI:
      GETPARAM(offs);
      pri=_R(data,offs);
      break;
    case AMX_OP_LOAD_ALT:
      GETPARAM(offs);
      alt=_R(data,offs);
      break;
    case AMX_OP_LOAD_S_PRI:
      GETPARAM(offs);
      pri=_R(data,frm+offs);
      break;
    case AMX_OP_LOAD_S_ALT:
      GETPARAM(offs);
      alt=_R(data,frm+offs);
      break;
    case AMX_OP_LREF_PRI:
      GETPARAM(offs);
      offs=_R(data,offs);
      pri=_R(data,offs);
      break;
    case AMX_OP_LREF_ALT:
      GETPARAM(offs);
      offs=_R(data,offs);
      alt=_R(data,offs);
      break;
    case AMX_OP_LREF_S_PRI:
      GETPARAM(offs);
      offs=_R(data,frm+offs);
      pri=_R(data,offs);
      break;
    case AMX_OP_LREF_S_ALT:
      GETPARAM(offs);
      offs=_R(data,frm+offs);
      alt=_R(data,offs);
      break;
    case AMX_OP_LOAD_I:
      /* verify address */
      if (pri>=hea && pri<stk || (ucell)pri>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      pri=_R(data,pri);
      break;
    case AMX_OP_LODB_I:
      GETPARAM(offs);
      /* verify address */
      if (pri>=hea && pri<stk || (ucell)pri>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      switch ((int)offs) {
      case 1:
        pri=_R8(data,pri);
        break;
      case 2:
        pri=_R16(data,pri);
        break;
      case 4:
        pri=_R32(data,pri);
        break;
      } /* switch */
      break;
    case AMX_OP_CONST_PRI:
      GETPARAM(pri);
      break;
    case AMX_OP_CONST_ALT:
      GETPARAM(alt);
      break;
    case AMX_OP_ADDR_PRI:
      GETPARAM(pri);
      pri+=frm;
      break;
    case AMX_OP_ADDR_ALT:
      GETPARAM(alt);
      alt+=frm;
      break;
    case AMX_OP_STOR_PRI:
      GETPARAM(offs);
      _W(data,offs,pri);
      break;
    case AMX_OP_STOR_ALT:
      GETPARAM(offs);
      _W(data,offs,alt);
      break;
    case AMX_OP_STOR_S_PRI:
      GETPARAM(offs);
      _W(data,frm+offs,pri);
      break;
    case AMX_OP_STOR_S_ALT:
      GETPARAM(offs);
      _W(data,frm+offs,alt);
      break;
    case AMX_OP_SREF_PRI:
      GETPARAM(offs);
      offs=_R(data,offs);
      _W(data,offs,pri);
      break;
    case AMX_OP_SREF_ALT:
      GETPARAM(offs);
      offs=_R(data,offs);
      _W(data,offs,alt);
      break;
    case AMX_OP_SREF_S_PRI:
      GETPARAM(offs);
      offs=_R(data,frm+offs);
      _W(data,offs,pri);
      break;
    case AMX_OP_SREF_S_ALT:
      GETPARAM(offs);
      offs=_R(data,frm+offs);
      _W(data,offs,alt);
      break;
    case AMX_OP_STOR_I:
      /* verify address */
      if (alt>=hea && alt<stk || (ucell)alt>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      _W(data,alt,pri);
      break;
    case AMX_OP_STRB_I:
      GETPARAM(offs);
      /* verify address */
      if (alt>=hea && alt<stk || (ucell)alt>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      switch ((int)offs) {
      case 1:
        _W8(data,alt,pri);
        break;
      case 2:
        _W16(data,alt,pri);
        break;
      case 4:
        _W32(data,alt,pri);
        break;
      } /* switch */
      break;
    case AMX_OP_LIDX:
      offs=pri*sizeof(cell)+alt;
      /* verify address */
      if (offs>=hea && offs<stk || (ucell)offs>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      pri=_R(data,offs);
      break;
    case AMX_OP_LIDX_B:
      GETPARAM(offs);
      offs=(pri << (int)offs)+alt;
      /* verify address */
      if (offs>=hea && offs<stk || (ucell)offs>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      pri=_R(data,offs);
      break;
    case AMX_OP_IDXADDR:
      pri=pri*sizeof(cell)+alt;
      break;
    case AMX_OP_IDXADDR_B:
      GETPARAM(offs);
      pri=(pri << (int)offs)+alt;
      break;
    case AMX_OP_ALIGN_PRI:
      GETPARAM(offs);
      #if BYTE_ORDER==LITTLE_ENDIAN
        if ((size_t)offs<sizeof(cell))
          pri ^= sizeof(cell)-offs;
      #endif
      break;
    case AMX_OP_ALIGN_ALT:
      GETPARAM(offs);
      #if BYTE_ORDER==LITTLE_ENDIAN
        if ((size_t)offs<sizeof(cell))
          alt ^= sizeof(cell)-offs;
      #endif
      break;
    case AMX_OP_LCTRL:
      GETPARAM(offs);
      switch ((int)offs) {
      case 0:
        pri=hdr->cod;
        break;
      case 1:
        pri=hdr->dat;
        break;
      case 2:
        pri=hea;
        break;
      case 3:
        pri=_amx->stp;
        break;
      case 4:
        pri=stk;
        break;
      case 5:
        pri=frm;
        break;
      case 6:
        pri=(cell)((unsigned char *)cip - code);
        break;
      } /* switch */
      break;
    case AMX_OP_SCTRL:
      GETPARAM(offs);
      switch ((int)offs) {
      case 0:
      case 1:
      case 3:
        /* cannot change these parameters */
        break;
      case 2:
        hea=pri;
        break;
      case 4:
        stk=pri;
        break;
      case 5:
        frm=pri;
        break;
      case 6:
        cip=(cell *)(code + (int)pri);
        break;
      } /* switch */
      break;
    case AMX_OP_MOVE_PRI:
      pri=alt;
      break;
    case AMX_OP_MOVE_ALT:
      alt=pri;
      break;
    case AMX_OP_XCHG:
      offs=pri;         /* offs is a temporary variable */
      pri=alt;
      alt=offs;
      break;
    case AMX_OP_PUSH_PRI:
      PUSH(pri);
      break;
    case AMX_OP_PUSH_ALT:
      PUSH(alt);
      break;
    case AMX_OP_PUSH_C:
      GETPARAM(offs);
      PUSH(offs);
      break;
    case AMX_OP_PUSH_R:
      GETPARAM(offs);
      while (offs--)
        PUSH(pri);
      break;
    case AMX_OP_PUSH:
      GETPARAM(offs);
      PUSH(_R(data,offs));
      break;
    case AMX_OP_PUSH_S:
      GETPARAM(offs);
      PUSH(_R(data,frm+offs));
      break;
    case AMX_OP_PAMX_OP_PRI:
      POP(pri);
      break;
    case AMX_OP_PAMX_OP_ALT:
      POP(alt);
      break;
    case AMX_OP_STACK:
      GETPARAM(offs);
      alt=stk;
      stk+=offs;
      CHKMARGIN();
      CHKSTACK();
      break;
    case AMX_OP_HEAP:
      GETPARAM(offs);
      alt=hea;
      hea+=offs;
      CHKMARGIN();
      CHKHEAP();
      break;
    case AMX_OP_PROC:
      PUSH(frm);
      frm=stk;
      CHKMARGIN();
      break;
    case AMX_OP_RET:
      POP(frm);
      POP(offs);
      /* verify the return address */
      if ((ucell)offs>=codesize)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      cip=(cell *)(code+(int)offs);
      break;
    case AMX_OP_RETN:
      POP(frm);
      POP(offs);
      /* verify the return address */
      if ((ucell)offs>=codesize)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      cip=(cell *)(code+(int)offs);
      stk+=_R(data,stk)+sizeof(cell);   /* remove parameters from the stack */
      _amx->stk=stk;
      break;
    case AMX_OP_CALL:
      PUSH(((unsigned char *)cip-code)+sizeof(cell));/* skip address */
      cip=JUMPABS(code, cip);                   /* jump to the address */
      break;
    case AMX_OP_CALL_PRI:
      PUSH((unsigned char *)cip-code);
      cip=(cell *)(code+(int)pri);
      break;
    case AMX_OP_JUMP:
      /* since the GETPARAM() macro modifies cip, you cannot
       * do GETPARAM(cip) directly */
      cip=JUMPABS(code, cip);
      break;
    case AMX_OP_JREL:
      offs=*cip;
      cip=(cell *)((unsigned char *)cip + (int)offs + sizeof(cell));
      break;
    case AMX_OP_JZER:
      if (pri==0)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case AMX_OP_JNZ:
      if (pri!=0)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case AMX_OP_JEQ:
      if (pri==alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case AMX_OP_JNEQ:
      if (pri!=alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case AMX_OP_JLESS:
      if ((ucell)pri < (ucell)alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case AMX_OP_JLEQ:
      if ((ucell)pri <= (ucell)alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case AMX_OP_JGRTR:
      if ((ucell)pri > (ucell)alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case AMX_OP_JGEQ:
      if ((ucell)pri >= (ucell)alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case AMX_OP_JSLESS:
      if (pri<alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case AMX_OP_JSLEQ:
      if (pri<=alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case AMX_OP_JSGRTR:
      if (pri>alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case AMX_OP_JSGEQ:
      if (pri>=alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case AMX_OP_SHL:
      pri<<=alt;
      break;
    case AMX_OP_SHR:
      pri=(ucell)pri >> (int)alt;
      break;
    case AMX_OP_SSHR:
      pri>>=alt;
      break;
    case AMX_OP_SHL_C_PRI:
      GETPARAM(offs);
      pri<<=offs;
      break;
    case AMX_OP_SHL_C_ALT:
      GETPARAM(offs);
      alt<<=offs;
      break;
    case AMX_OP_SHR_C_PRI:
      GETPARAM(offs);
      pri=(ucell)pri >> (int)offs;
      break;
    case AMX_OP_SHR_C_ALT:
      GETPARAM(offs);
      alt=(ucell)alt >> (int)offs;
      break;
    case AMX_OP_SMUL:
      pri*=alt;
      break;
    case AMX_OP_SDIV:
      if (alt==0)
        ABORT(_amx,AMX_ERR_DIVIDE);
      /* use floored division and matching remainder */
      offs=alt;
      #if defined TRUNC_SDIV
        pri=pri/offs;
        alt=pri%offs;
      #else
        val=pri;                /* portable routine for truncated division */
        pri=IABS(pri)/IABS(offs);
        if ((cell)(val ^ offs)<0)
          pri=-pri;
        alt=val-pri*offs;       /* calculate the matching remainder */
      #endif
      /* now "fiddle" with the values to get floored division */
      if (alt!=0 && (cell)(alt ^ offs)<0) {
        pri--;
        alt+=offs;
      } /* if */
      break;
    case AMX_OP_SDIV_ALT:
      if (pri==0)
        ABORT(_amx,AMX_ERR_DIVIDE);
      /* use floored division and matching remainder */
      offs=pri;
      #if defined TRUNC_SDIV
        pri=alt/offs;
        alt=alt%offs;
      #else
        val=alt;                /* portable routine for truncated division */
        pri=IABS(alt)/IABS(offs);
        if ((cell)(val ^ offs)<0)
          pri=-pri;
        alt=val-pri*offs;       /* calculate the matching remainder */
      #endif
      /* now "fiddle" with the values to get floored division */
      if (alt!=0 && (cell)(alt ^ offs)<0) {
        pri--;
        alt+=offs;
      } /* if */
      break;
    case AMX_OP_UMUL:
      pri=(ucell)pri * (ucell)alt;
      break;
    case AMX_OP_UDIV:
      if (alt==0)
        ABORT(_amx,AMX_ERR_DIVIDE);
      offs=(ucell)pri % (ucell)alt;     /* temporary storage */
      pri=(ucell)pri / (ucell)alt;
      alt=offs;
      break;
    case AMX_OP_UDIV_ALT:
      if (pri==0)
        ABORT(_amx,AMX_ERR_DIVIDE);
      offs=(ucell)alt % (ucell)pri;     /* temporary storage */
      pri=(ucell)alt / (ucell)pri;
      alt=offs;
      break;
    case AMX_OP_ADD:
      pri+=alt;
      break;
    case AMX_OP_SUB:
      pri-=alt;
      break;
    case AMX_OP_SUB_ALT:
      pri=alt-pri;
      break;
    case AMX_OP_AND:
      pri&=alt;
      break;
    case AMX_OP_OR:
      pri|=alt;
      break;
    case AMX_OP_XOR:
      pri^=alt;
      break;
    case AMX_OP_NOT:
      pri=!pri;
      break;
    case AMX_OP_NEG:
      pri=-pri;
      break;
    case AMX_OP_INVERT:
      pri=~pri;
      break;
    case AMX_OP_ADD_C:
      GETPARAM(offs);
      pri+=offs;
      break;
    case AMX_OP_SMUL_C:
      GETPARAM(offs);
      pri*=offs;
      break;
    case AMX_OP_ZERO_PRI:
      pri=0;
      break;
    case AMX_OP_ZERO_ALT:
      alt=0;
      break;
    case AMX_OP_ZERO:
      GETPARAM(offs);
      _W(data,offs,0);
      break;
    case AMX_OP_ZERO_S:
      GETPARAM(offs);
      _W(data,frm+offs,0);
      break;
    case AMX_OP_SIGN_PRI:
      if ((pri & 0xff)>=0x80)
        pri|= ~ (ucell)0xff;
      break;
    case AMX_OP_SIGN_ALT:
      if ((alt & 0xff)>=0x80)
        alt|= ~ (ucell)0xff;
      break;
    case AMX_OP_EQ:
      pri= pri==alt ? 1 : 0;
      break;
    case AMX_OP_NEQ:
      pri= pri!=alt ? 1 : 0;
      break;
    case AMX_OP_LESS:
      pri= (ucell)pri < (ucell)alt ? 1 : 0;
      break;
    case AMX_OP_LEQ:
      pri= (ucell)pri <= (ucell)alt ? 1 : 0;
      break;
    case AMX_OP_GRTR:
      pri= (ucell)pri > (ucell)alt ? 1 : 0;
      break;
    case AMX_OP_GEQ:
      pri= (ucell)pri >= (ucell)alt ? 1 : 0;
      break;
    case AMX_OP_SLESS:
      pri= pri<alt ? 1 : 0;
      break;
    case AMX_OP_SLEQ:
      pri= pri<=alt ? 1 : 0;
      break;
    case AMX_OP_SGRTR:
      pri= pri>alt ? 1 : 0;
      break;
    case AMX_OP_SGEQ:
      pri= pri>=alt ? 1 : 0;
      break;
    case AMX_OP_EQ_C_PRI:
      GETPARAM(offs);
      pri= pri==offs ? 1 : 0;
      break;
    case AMX_OP_EQ_C_ALT:
      GETPARAM(offs);
      pri= alt==offs ? 1 : 0;
      break;
    case AMX_OP_INC_PRI:
      pri++;
      break;
    case AMX_OP_INC_ALT:
      alt++;
      break;
    case AMX_OP_INC:
      GETPARAM(offs);
      #if defined _R_DEFAULT
        *(cell *)(data+(int)offs) += 1;
      #else
        val=_R(data,offs);
        _W(data,offs,val+1);
      #endif
      break;
    case AMX_OP_INC_S:
      GETPARAM(offs);
      #if defined _R_DEFAULT
        *(cell *)(data+(int)(frm+offs)) += 1;
      #else
        val=_R(data,frm+offs);
        _W(data,frm+offs,val+1);
      #endif
      break;
    case AMX_OP_INC_I:
      #if defined _R_DEFAULT
        *(cell *)(data+(int)pri) += 1;
      #else
        val=_R(data,pri);
        _W(data,pri,val+1);
      #endif
      break;
    case AMX_OP_DEC_PRI:
      pri--;
      break;
    case AMX_OP_DEC_ALT:
      alt--;
      break;
    case AMX_OP_DEC:
      GETPARAM(offs);
      #if defined _R_DEFAULT
        *(cell *)(data+(int)offs) -= 1;
      #else
        val=_R(data,offs);
        _W(data,offs,val-1);
      #endif
      break;
    case AMX_OP_DEC_S:
      GETPARAM(offs);
      #if defined _R_DEFAULT
        *(cell *)(data+(int)(frm+offs)) -= 1;
      #else
        val=_R(data,frm+offs);
        _W(data,frm+offs,val-1);
      #endif
      break;
    case AMX_OP_DEC_I:
      #if defined _R_DEFAULT
        *(cell *)(data+(int)pri) -= 1;
      #else
        val=_R(data,pri);
        _W(data,pri,val-1);
      #endif
      break;
    case AMX_OP_MOVS:
      GETPARAM(offs);
      /* verify top & bottom memory addresses, for both source and destination
       * addresses
       */
      if (pri>=hea && pri<stk || (ucell)pri>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      if ((pri+offs)>hea && (pri+offs)<stk || (ucell)(pri+offs)>(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      if (alt>=hea && alt<stk || (ucell)alt>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      if ((alt+offs)>hea && (alt+offs)<stk || (ucell)(alt+offs)>(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      #if defined _R_DEFAULT
        memcpy(data+(int)alt, data+(int)pri, (int)offs);
      #else
        for (i=0; i+4<offs; i+=4) {
          val=_R32(data,pri+i);
          _W32(data,alt+i,val);
        } /* for */
        for ( ; i<offs; i++) {
          val=_R8(data,pri+i);
          _W8(data,alt+i,val);
        } /* for */
      #endif
      break;
    case AMX_OP_CMPS:
      GETPARAM(offs);
      /* verify top & bottom memory addresses, for both source and destination
       * addresses
       */
      if (pri>=hea && pri<stk || (ucell)pri>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      if ((pri+offs)>hea && (pri+offs)<stk || (ucell)(pri+offs)>(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      if (alt>=hea && alt<stk || (ucell)alt>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      if ((alt+offs)>hea && (alt+offs)<stk || (ucell)(alt+offs)>(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      #if defined _R_DEFAULT
        pri=memcmp(data+(int)alt, data+(int)pri, (int)offs);
      #else
        pri=0;
        for (i=0; i+4<offs && pri==0; i+=4)
          pri=_R32(data,alt+i)-_R32(data,pri+i);
        for ( ; i<offs && pri==0; i++)
          pri=_R8(data,alt+i)-_R8(data,pri+i);
      #endif
      break;
    case AMX_OP_FILL:
      GETPARAM(offs);
      /* verify top & bottom memory addresses (destination only) */
      if (alt>=hea && alt<stk || (ucell)alt>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      if ((alt+offs)>hea && (alt+offs)<stk || (ucell)(alt+offs)>(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      for (i=(int)alt; (size_t)offs>=sizeof(cell); i+=sizeof(cell), offs-=sizeof(cell))
        _W32(data,i,pri);
      break;
    case AMX_OP_HALT:
      GETPARAM(offs);
      if (retval!=NULL)
        *retval=pri;
      /* store complete status (stk and hea are already set in the ABORT macro) */
      _amx->frm=frm;
      _amx->pri=pri;
      _amx->alt=alt;
      _amx->cip=(cell)((unsigned char*)cip-code);
      if (offs==AMX_ERR_SLEEP) {
        _amx->stk=stk;
        _amx->hea=hea;
        _amx->reset_stk=reset_stk;
        _amx->reset_hea=reset_hea;
        return (int)offs;
      } /* if */
      ABORT(_amx,(int)offs);
    case AMX_OP_BOUNDS:
      GETPARAM(offs);
      if ((ucell)pri>(ucell)offs) {
        _amx->cip=(cell)((unsigned char *)cip-code);
        ABORT(_amx,AMX_ERR_BOUNDS);
      } /* if */
      break;
    case AMX_OP_SYSREQ_PRI:
      /* save a few registers */
      _amx->cip=(cell)((unsigned char *)cip-code);
      _amx->hea=hea;
      _amx->frm=frm;
      _amx->stk=stk;
      num=_amx->callback(_amx,pri,&pri,(cell *)(data+(int)stk));
      if (num!=AMX_ERR_NONE) {
        if (num==AMX_ERR_SLEEP) {
          _amx->pri=pri;
          _amx->alt=alt;
          _amx->reset_stk=reset_stk;
          _amx->reset_hea=reset_hea;
          return num;
        } /* if */
        ABORT(_amx,num);
      } /* if */
      break;
    case AMX_OP_SYSREQ_C:
      GETPARAM(offs);
      /* save a few registers */
      _amx->cip=(cell)((unsigned char *)cip-code);
      _amx->hea=hea;
      _amx->frm=frm;
      _amx->stk=stk;
      num=_amx->callback(_amx,offs,&pri,(cell *)(data+(int)stk));
      if (num!=AMX_ERR_NONE) {
        if (num==AMX_ERR_SLEEP) {
          _amx->pri=pri;
          _amx->alt=alt;
          _amx->reset_stk=reset_stk;
          _amx->reset_hea=reset_hea;
          return num;
        } /* if */
        ABORT(_amx,num);
      } /* if */
      break;
    case AMX_OP_LINE:
      SKIPPARAM(2);
      break;
    case AMX_OP_SYMBOL:
      GETPARAM(offs);
      cip=(cell *)((unsigned char *)cip + (int)offs);
      break;
    case AMX_OP_SRANGE:
      SKIPPARAM(2);
      break;
    case AMX_OP_SYMTAG:
      SKIPPARAM(1);
      break;
    case AMX_OP_JUMP_PRI:
      cip=(cell *)(code+(int)pri);
      break;
    case AMX_OP_SWITCH: {
      cell *cptr;

      cptr=JUMPABS(code,cip)+1; /* +1, to skip the "casetbl" opcode */
      cip=JUMPABS(code,cptr+1); /* preset to "none-matched" case */
      num=(int)*cptr;           /* number of records in the case table */
      for (cptr+=2; num>0 && *cptr!=pri; num--,cptr+=2)
        /* nothing */;
      if (num>0)
        cip=JUMPABS(code,cptr+1); /* case found */
      break;
    } /* case */
    case AMX_OP_SWAP_PRI:
      offs=_R(data,stk);
      _W32(data,stk,pri);
      pri=offs;
      break;
    case AMX_OP_SWAP_ALT:
      offs=_R(data,stk);
      _W32(data,stk,alt);
      alt=offs;
      break;
    case AMX_OP_PUSH_ADR:
      GETPARAM(offs);
      PUSH(frm+offs);
      break;
    case AMX_OP_NOP:
      break;
#if !defined AMX_NO_MACRO_INSTR
    case AMX_OP_SYSREQ_N:
      GETPARAM(offs);
      GETPARAM(val);
      PUSH(val);
      /* save a few registers */
      _amx->cip=(cell)((unsigned char *)cip-code);
      _amx->hea=hea;
      _amx->frm=frm;
      _amx->stk=stk;
      num=_amx->callback(_amx,offs,&pri,(cell *)(data+(int)stk));
      stk+=val+4;
      if (num!=AMX_ERR_NONE) {
        if (num==AMX_ERR_SLEEP) {
          _amx->pri=pri;
          _amx->alt=alt;
          _amx->reset_stk=reset_stk;
          _amx->reset_hea=reset_hea;
          return num;
        } /* if */
        ABORT(_amx,num);
      } /* if */
      break;
#endif
    case AMX_OP_BREAK:
      assert((_amx->flags & AMX_FLAG_BROWSE)==0);
      if (_amx->debug!=NULL) {
        /* store status */
        _amx->frm=frm;
        _amx->stk=stk;
        _amx->hea=hea;
        _amx->cip=(cell)((unsigned char*)cip-code);
        num=_amx->debug(_amx);
        if (num!=AMX_ERR_NONE) {
          if (num==AMX_ERR_SLEEP) {
            _amx->pri=pri;
            _amx->alt=alt;
            _amx->reset_stk=reset_stk;
            _amx->reset_hea=reset_hea;
            return num;
          } /* if */
          ABORT(_amx,num);
        } /* if */
      } /* if */
      break;
#if !defined AMX_NO_MACRO_INSTR
    case AMX_OP_PUSH5:
      GETPARAM(offs);
      PUSH(_R(data,offs));
      /* drop through */
    case AMX_OP_PUSH4:
      GETPARAM(offs);
      PUSH(_R(data,offs));
      /* drop through */
    case AMX_OP_PUSH3:
      GETPARAM(offs);
      PUSH(_R(data,offs));
      /* drop through */
    case AMX_OP_PUSH2:
      GETPARAM(offs);
      PUSH(_R(data,offs));
      GETPARAM(offs);
      PUSH(_R(data,offs));
      break;
    case AMX_OP_PUSH5_S:
      GETPARAM(offs);
      PUSH(_R(data,frm+offs));
      /* drop through */
    case AMX_OP_PUSH4_S:
      GETPARAM(offs);
      PUSH(_R(data,frm+offs));
      /* drop through */
    case AMX_OP_PUSH3_S:
      GETPARAM(offs);
      PUSH(_R(data,frm+offs));
      /* drop through */
    case AMX_OP_PUSH2_S:
      GETPARAM(offs);
      PUSH(_R(data,frm+offs));
      GETPARAM(offs);
      PUSH(_R(data,frm+offs));
      break;
    case AMX_OP_PUSH5_C:
      GETPARAM(offs);
      PUSH(offs);
      /* drop through */
    case AMX_OP_PUSH4_C:
      GETPARAM(offs);
      PUSH(offs);
      /* drop through */
    case AMX_OP_PUSH3_C:
      GETPARAM(offs);
      PUSH(offs);
      /* drop through */
    case AMX_OP_PUSH2_C:
      GETPARAM(offs);
      PUSH(offs);
      GETPARAM(offs);
      PUSH(offs);
      break;
    case AMX_OP_PUSH5_ADR:
      GETPARAM(offs);
      PUSH(frm+offs);
      /* drop through */
    case AMX_OP_PUSH4_ADR:
      GETPARAM(offs);
      PUSH(frm+offs);
      /* drop through */
    case AMX_OP_PUSH3_ADR:
      GETPARAM(offs);
      PUSH(frm+offs);
      /* drop through */
    case AMX_OP_PUSH2_ADR:
      GETPARAM(offs);
      PUSH(frm+offs);
      GETPARAM(offs);
      PUSH(frm+offs);
      break;
    case AMX_OP_LOAD_BOTH:
      GETPARAM(offs);
      pri=_R(data,offs);
      GETPARAM(offs);
      alt=_R(data,offs);
      break;
    case AMX_OP_LOAD_S_BOTH:
      GETPARAM(offs);
      pri=_R(data,frm+offs);
      GETPARAM(offs);
      alt=_R(data,frm+offs);
      break;
    case AMX_OP_CONST:
      GETPARAM(offs);
      GETPARAM(val);
      _W32(data,offs,val);
      break;
    case AMX_OP_CONST_S:
      GETPARAM(offs);
      GETPARAM(val);
      _W32(data,frm+offs,val);
      break;
#endif
#if !defined AMX_DONT_RELOCATE
    case AMX_OP_SYSREQ_D: /* see AMX_OP_SYSREQ_C */
      GETPARAM(offs);
      /* save a few registers */
      _amx->cip=(cell)((unsigned char *)cip-code);
      _amx->hea=hea;
      _amx->frm=frm;
      _amx->stk=stk;
      pri=((AMX_NATIVE)offs)(_amx,(cell *)(data+(int)stk));
      if (_amx->error!=AMX_ERR_NONE) {
        if (_amx->error==AMX_ERR_SLEEP) {
          _amx->pri=pri;
          _amx->alt=alt;
          _amx->reset_stk=reset_stk;
          _amx->reset_hea=reset_hea;
          return AMX_ERR_SLEEP;
        } /* if */
        ABORT(_amx,_amx->error);
      } /* if */
      break;
#endif
#if !defined AMX_NO_MACRO_INSTR && !defined AMX_DONT_RELOCATE
    case AMX_OP_SYSREQ_ND:    /* see SYSREQ_N */
      GETPARAM(offs);
      GETPARAM(val);
      PUSH(val);
      /* save a few registers */
      _amx->cip=(cell)((unsigned char *)cip-code);
      _amx->hea=hea;
      _amx->frm=frm;
      _amx->stk=stk;
      pri=((AMX_NATIVE)offs)(_amx,(cell *)(data+(int)stk));
      stk+=val+4;
      if (_amx->error!=AMX_ERR_NONE) {
        if (_amx->error==AMX_ERR_SLEEP) {
          _amx->pri=pri;
          _amx->alt=alt;
          _amx->reset_stk=reset_stk;
          _amx->reset_hea=reset_hea;
          return AMX_ERR_SLEEP;
        } /* if */
        ABORT(_amx,_amx->error);
      } /* if */
      break;
#endif
    default:
      /* case AMX_OP_FILE:          should not occur during execution
       * case AMX_OP_CASETBL:       should not occur during execution
       */
      assert(0);
      ABORT(_amx,AMX_ERR_INVINSTR);
    } /* switch */
  } /* for */
#endif
}