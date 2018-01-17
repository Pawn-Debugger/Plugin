#include <cassert>
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
}

AMXExecutor::AMXExecutor(AMX *amx) : AMXService<AMXExecutor>(amx)
{}

const cell STKMARGIN = (cell) (16 * sizeof(cell));

#define NUMENTRIES(hdr,field,nextfield) \
                        (unsigned)(((hdr)->nextfield - (hdr)->field) / (hdr)->defsize)
#define GETENTRY(hdr,table,index) \
                        (AMX_FUNCSTUB *)((unsigned char*)(hdr) + (unsigned)(hdr)->table + (unsigned)index*(hdr)->defsize)
#define CHKSTACK()      if (stk>_amx->stp) ABORT(_amx, AMX_ERR_STACKLOW)
#define CHKHEAP()       if (hea<_amx->hlw) ABORT(_amx, AMX_ERR_HEAPLOW)
#define CHKMARGIN()     if (hea+STKMARGIN>stk) ABORT(_amx, AMX_ERR_STACKERR)
        
#define GETPARAM(v)     ( v=*(cell *)cip++ )
#define SKIPPARAM(n)    ( cip=(cell *)cip+(n) )
#define PUSH(v)         ( stk-=sizeof(cell), *(cell *)(data+(int)stk)=v )
#define POP(v)          ( v=*(cell *)(data+(int)stk), stk+=sizeof(cell) )
#define ABORT(amx,v)    { (amx)->pri = pri;\
                          (amx)->stk = stk;\
                          (amx)->hea = hea;\
                          (amx)->frm = frm;\
                          amx_RaiseExecError(amx, index, retval, v);\
                          (amx)->stk=reset_stk;\
                          (amx)->hea=reset_hea;\
                          return v; }

#if PAWN_CELL_SIZE==16
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
  AMXOpcode op;
  cell offs;
  int num;

  assert(_amx!=NULL);

  if (_amx->callback==NULL)
    return AMX_ERR_CALLBACK;
  if ((_amx->flags & AMX_FLAG_NTVREG)==0)
    return AMX_ERR_NOTFOUND;
  if ((_amx->flags & AMX_FLAG_RELOC)==0)
    return AMX_ERR_INIT;
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
  frm=alt=pri=0;        /* silence up compiler */

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
  assert(AMX_OP_PUSH_PRI==36);
  assert(AMX_OP_PROC==46);
  assert(AMX_OP_SHL==65);
  assert(AMX_OP_SMUL==72);
  assert(AMX_OP_EQ==95);
  assert(AMX_OP_INC_PRI==107);
  assert(AMX_OP_MOVS==117);
  assert(AMX_OP_SYMBOL==126);
  #if PAWN_CELL_SIZE==16
    assert(sizeof(cell)==2);
  #elif PAWN_CELL_SIZE==32
    assert(sizeof(cell)==4);
  #elif PAWN_CELL_SIZE==64
    assert(sizeof(cell)==8);
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

  for ( ;; ) {  
    _amx->cip=(cell)cip-(cell)code;
    LogDebugPrint("Current CIP: %d | Current OP: %s | Param or next OP: %d",
      (uint32_t) cip,
      std::string(AMXOpcodeNames[*cip]).c_str(),
      *(cip + 1)
    );
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    op=(AMXOpcode) *cip++;
    switch (op) {
    case AMX_OP_LOAD_PRI:
      GETPARAM(offs);
      pri= * (cell *)(data+(int)offs);
      break;
    case AMX_OP_LOAD_ALT:
      GETPARAM(offs);
      alt= * (cell *)(data+(int)offs);
      break;
    case AMX_OP_LOAD_S_PRI:
      GETPARAM(offs);
      pri= * (cell *)(data+(int)frm+(int)offs);
      break;
    case AMX_OP_LOAD_S_ALT:
      GETPARAM(offs);
      alt= * (cell *)(data+(int)frm+(int)offs);
      break;
    case AMX_OP_LREF_PRI:
      GETPARAM(offs);
      offs= * (cell *)(data+(int)offs);
      pri= * (cell *)(data+(int)offs);
      break;
    case AMX_OP_LREF_ALT:
      GETPARAM(offs);
      offs= * (cell *)(data+(int)offs);
      alt= * (cell *)(data+(int)offs);
      break;
    case AMX_OP_LREF_S_PRI:
      _amx->frm=frm;
      _amx->stk=stk;
      GETPARAM(offs);
      offs= * (cell *)(data+(int)frm+(int)offs);
      pri= * (cell *)(data+(int)offs);
      break;
    case AMX_OP_LREF_S_ALT:
      _amx->frm=frm;
      _amx->stk=stk;  
      GETPARAM(offs);
      offs= * (cell *)(data+(int)frm+(int)offs);
      alt= * (cell *)(data+(int)offs);
      break;
    case AMX_OP_LOAD_I:
      /* verify address */
      if (pri>=hea && pri<stk || (ucell)pri>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      pri= * (cell *)(data+(int)pri);
      break;
    case AMX_OP_LODB_I:
      GETPARAM(offs);
      /* verify address */
      if (pri>=hea && pri<stk || (ucell)pri>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      switch (offs) {
      case 1:
        pri= * (data+(int)pri);
        break;
      case 2:
        pri= * (uint16_t *)(data+(int)pri);
        break;
      case 4:
        pri= * (uint32_t *)(data+(int)pri);
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
      *(cell *)(data+(int)offs)=pri;
      break;
    case AMX_OP_STOR_ALT:
      GETPARAM(offs);
      *(cell *)(data+(int)offs)=alt;
      break;
    case AMX_OP_STOR_S_PRI:
      GETPARAM(offs);
      *(cell *)(data+(int)frm+(int)offs)=pri;
      break;
    case AMX_OP_STOR_S_ALT:
      GETPARAM(offs);
      *(cell *)(data+(int)frm+(int)offs)=alt;
      break;
    case AMX_OP_SREF_PRI:
      _amx->frm=frm;
      _amx->stk=stk;
      GETPARAM(offs);
      offs= * (cell *)(data+(int)offs);
      *(cell *)(data+(int)offs)=pri;
      break;
    case AMX_OP_SREF_ALT:
      _amx->frm = frm;
      _amx->stk = stk;
      GETPARAM(offs);
      offs= * (cell *)(data+(int)offs);
      *(cell *)(data+(int)offs)=alt;
      break;
    case AMX_OP_SREF_S_PRI:
      GETPARAM(offs);
      offs= * (cell *)(data+(int)frm+(int)offs);
      *(cell *)(data+(int)offs)=pri;
      break;
    case AMX_OP_SREF_S_ALT:
      GETPARAM(offs);
      offs= * (cell *)(data+(int)frm+(int)offs);
      *(cell *)(data+(int)offs)=alt;
      break;
    case AMX_OP_STOR_I:
      /* verify address */
      if (alt>=hea && alt<stk || (ucell)alt>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      *(cell *)(data+(int)alt)=pri;
      break;
    case AMX_OP_STRB_I:
      GETPARAM(offs);
      /* verify address */
      if (alt>=hea && alt<stk || (ucell)alt>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      switch (offs) {
      case 1:
        *(data+(int)alt)=(unsigned char)pri;
        break;
      case 2:
        *(uint16_t *)(data+(int)alt)=(uint16_t)pri;
        break;
      case 4:
        *(uint32_t *)(data+(int)alt)=(uint32_t)pri;
        break;
      } /* switch */
      break;
    case AMX_OP_LIDX:
      offs=pri*sizeof(cell)+alt;
      /* verify address */
      if (offs>=hea && offs<stk || (ucell)offs>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      pri= * (cell *)(data+(int)offs);
      break;
    case AMX_OP_LIDX_B:
      GETPARAM(offs);
      offs=(pri << (int)offs)+alt;
      /* verify address */
      if (offs>=hea && offs<stk || (ucell)offs>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      pri= * (cell *)(data+(int)offs);
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
      switch (offs) {
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
      case 0xFF:
        pri=1;
        break;
      } /* switch */
      break;
    case AMX_OP_SCTRL:
      GETPARAM(offs);
      switch (offs) {
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
      PUSH(* (cell *)(data+(int)offs));
      break;
    case AMX_OP_PUSH_S:
      GETPARAM(offs);
      PUSH(* (cell *)(data+(int)frm+(int)offs));
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
      stk+= *(cell *)(data+(int)stk) + sizeof(cell); /* remove parameters from the stack */
      _amx->stk=stk;
      break;
    case AMX_OP_CALL:
      PUSH(((unsigned char *)cip-code)+sizeof(cell));/* skip address */
      cip=JUMPABS(code, cip);                   /* jump to the address */
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
      /* divide must always round down; this is a bit
       * involved to do in a machine-independent way.
       */
      offs=(pri % alt + alt) % alt;     /* true modulus */
      pri=(pri - offs) / alt;           /* division result */
      alt=offs;
      break;
    case AMX_OP_SDIV_ALT:
      if (pri==0)
        ABORT(_amx,AMX_ERR_DIVIDE);
      /* divide must always round down; this is a bit
       * involved to do in a machine-independent way.
       */
      offs=(alt % pri + pri) % pri;     /* true modulus */
      pri=(alt - offs) / pri;           /* division result */
      alt=offs;
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
      *(cell *)(data+(int)offs)=0;
      break;
    case AMX_OP_ZERO_S:
      GETPARAM(offs);
      *(cell *)(data+(int)frm+(int)offs)=0;
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
      *(cell *)(data+(int)offs) += 1;
      break;
    case AMX_OP_INC_S:
      GETPARAM(offs);
      *(cell *)(data+(int)frm+(int)offs) += 1;
      break;
    case AMX_OP_INC_I:
      *(cell *)(data+(int)pri) += 1;
      break;
    case AMX_OP_DEC_PRI:
      pri--;
      break;
    case AMX_OP_DEC_ALT:
      alt--;
      break;
    case AMX_OP_DEC:
      GETPARAM(offs);
      *(cell *)(data+(int)offs) -= 1;
      break;
    case AMX_OP_DEC_S:
      GETPARAM(offs);
      *(cell *)(data+(int)frm+(int)offs) -= 1;
      break;
    case AMX_OP_DEC_I:
      *(cell *)(data+(int)pri) -= 1;
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
      memcpy(data+(int)alt, data+(int)pri, (int)offs);
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
      pri=memcmp(data+(int)alt, data+(int)pri, (int)offs);
      break;
    case AMX_OP_FILL:
      GETPARAM(offs);
      /* verify top & bottom memory addresses (destination only) */
      if (alt>=hea && alt<stk || (ucell)alt>=(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      if ((alt+offs)>hea && (alt+offs)<stk || (ucell)(alt+offs)>(ucell)_amx->stp)
        ABORT(_amx,AMX_ERR_MEMACCESS);
      for (i=(int)alt; (size_t)offs>=sizeof(cell); i+=sizeof(cell), offs-=sizeof(cell))
        *(cell *)(data+i) = pri;
      break;
    case AMX_OP_HALT:
      GETPARAM(offs);
      if (retval!=NULL)
        *retval=pri;
      /* store complete status (stk and hea are already set in the ABORT macro) */
      _amx->frm=frm;
      _amx->pri=pri;
      _amx->alt=alt;
      if (offs!=AMX_ERR_SLEEP) {
        ABORT(_amx,(int)offs);
      } else {
        _amx->cip=(cell)((unsigned char*)cip-code);
        _amx->reset_stk=reset_stk;
        _amx->reset_hea=reset_hea;
        return (int)offs;
      } /* if */
    case AMX_OP_BOUNDS:
      GETPARAM(offs);
      if ((ucell)pri>(ucell)offs)
        ABORT(_amx,AMX_ERR_BOUNDS);
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
    case AMX_OP_SYSREQ_D:
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
          return num;
        } /* if */
        ABORT(_amx,_amx->error);
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
      offs=*(cell *)(data+(int)stk);
      *(cell *)(data+(int)stk)=pri;
      pri=offs;
      break;
    case AMX_OP_SWAP_ALT:
      offs=*(cell *)(data+(int)stk);
      *(cell *)(data+(int)stk)=alt;
      alt=offs;
      break;
    case AMX_OP_PUSH_ADR:
      GETPARAM(offs);
      PUSH(frm+offs);
      break;
    case AMX_OP_NOP:
      break;
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
    default:
      /* case AMX_OP_FILE:          should not occur during execution
       * case AMX_OP_CASETBL:       should not occur during execution
       */
      assert(0);
      ABORT(_amx,AMX_ERR_INVINSTR);
    } /* switch */
  } /* for */
}