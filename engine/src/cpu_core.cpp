// Standalone port of MAME's HD6303X CPU core.
// Register/macro layer below is a direct port of src/devices/cpu/m6800/m6800.cpp
// (license:BSD-3-Clause, copyright-holders:Aaron Giles); the opcode bodies in
// 6800ops.hxx are copied verbatim from the same project (same license/author).
// The hd63701_insn/cycles_63701 tables are ported from m6801.cpp (same license/author).
#include "tx81z/cpu_core.h"

#include <cstdio>
#include <cstdarg>

#define pPPC    m_ppc
#define pPC     m_pc
#define pS      m_s
#define pX      m_x
#define pD      m_d

#define PC      m_pc.w.l
#define PCD     m_pc.d
#define S       m_s.w.l
#define SD      m_s.d
#define X       m_x.w.l
#define D       m_d.w.l
#define A       m_d.b.h
#define B       m_d.b.l
#define CC      m_cc

#define EAD     m_ea.d
#define EA      m_ea.w.l

/* memory interface */
#define RM(Addr) (read(Addr))
#define WM(Addr,Value) (write(Addr,Value))
#define M_RDOP(Addr) (read(Addr))
#define M_RDOP_ARG(Addr) (read(Addr))

/* macros to access memory */
#define IMMBYTE(b)  b = M_RDOP_ARG(PCD); PC++
#define IMMWORD(w)  w.d = (M_RDOP_ARG(PCD)<<8) | M_RDOP_ARG((PCD+1)&0xffff); PC+=2

#define PUSHBYTE(b) WM(SD,b); --S
#define PUSHWORD(w) WM(SD,w.b.l); --S; WM(SD,w.b.h); --S
#define PULLBYTE(b) S++; b = RM(SD)
#define PULLWORD(w) S++; w.d = RM(SD)<<8; S++; w.d |= RM(SD)

/* CC masks                       HI NZVC
                                7654 3210   */
#define CLR_HNZVC   CC&=0xd0
#define CLR_NZV     CC&=0xf1
#define CLR_HNZC    CC&=0xd2
#define CLR_NZVC    CC&=0xf0
#define CLR_Z       CC&=0xfb
#define CLR_ZC      CC&=0xfa
#define CLR_C       CC&=0xfe

/* macros for CC -- CC bits affected should be reset before calling */
#define SET_Z(a)        if(!(a))SEZ
#define SET_Z8(a)       SET_Z(u8(a))
#define SET_Z16(a)      SET_Z(u16(a))
#define SET_N8(a)       CC|=(((a)&0x80)>>4)
#define SET_N16(a)      CC|=(((a)&0x8000)>>12)
#define SET_H(a,b,r)    CC|=((((a)^(b)^(r))&0x10)<<1)
#define SET_C8(a)       CC|=(((a)&0x100)>>8)
#define SET_C16(a)      CC|=(((a)&0x10000)>>16)
#define SET_V8(a,b,r)   CC|=((((a)^(b)^(r)^((r)>>1))&0x80)>>6)
#define SET_V16(a,b,r)  CC|=((((a)^(b)^(r)^((r)>>1))&0x8000)>>14)

const u8 m6800_cpu_device::flags8i[256]= /* increment */
{
0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x0a,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08
};

const u8 m6800_cpu_device::flags8d[256]= /* decrement */
{
0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08
};

#define SET_FLAGS8I(a)      {CC|=flags8i[(a)&0xff];}
#define SET_FLAGS8D(a)      {CC|=flags8d[(a)&0xff];}

/* combos */
#define SET_NZ8(a)          {SET_N8(a);SET_Z8(a);}
#define SET_NZ16(a)         {SET_N16(a);SET_Z16(a);}
#define SET_FLAGS8(a,b,r)   {SET_N8(r);SET_Z8(r);SET_V8(a,b,r);SET_C8(r);}
#define SET_FLAGS16(a,b,r)  {SET_N16(r);SET_Z16(r);SET_V16(a,b,r);SET_C16(r);}

/* for treating an u8 as a signed s16 */
#define SIGNED(b) (s16(b&0x80?b|0xff00:b))

/* Macros for addressing modes */
#define DIRECT IMMBYTE(EAD)
#define IMM8 EA=PC++
#define IMM16 {EA=PC;PC+=2;}
#define EXTENDED IMMWORD(m_ea)
#define INDEXED {EA=X+(u8)M_RDOP_ARG(PCD);PC++;}

/* macros to set status flags */
#if defined(SEC)
#undef SEC
#endif
#define SEC CC|=0x01
#define CLC CC&=0xfe
#define SEZ CC|=0x04
#define CLZ CC&=0xfb
#define SEN CC|=0x08
#define CLN CC&=0xf7
#define SEV CC|=0x02
#define CLV CC&=0xfd
#define SEH CC|=0x20
#define CLH CC&=0xdf
#define SEI CC|=0x10
#define CLI CC&=~0x10

/* macros for convenience */
#define DIRBYTE(b) {DIRECT;b=RM(EAD);}
#define DIRWORD(w) {DIRECT;w.d=RM16(EAD);}
#define EXTBYTE(b) {EXTENDED;b=RM(EAD);}
#define EXTWORD(w) {EXTENDED;w.d=RM16(EAD);}

#define IDXBYTE(b) {INDEXED;b=RM(EAD);}
#define IDXWORD(w) {INDEXED;w.d=RM16(EAD);}

/* Macros for branch instructions */
#define BRANCH(f) {IMMBYTE(t);if(f){PC+=SIGNED(t);}}
#define NXORV  ((CC&0x08)^((CC&0x02)<<2))
#define NXORC  ((CC&0x08)^((CC&0x01)<<3))

void m6800_cpu_device::logerror(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	std::vfprintf(stderr, fmt, ap);
	va_end(ap);
}

/* include the opcode functions (copied verbatim from MAME's 6800ops.hxx) */
#include "6800ops.hxx"

// HD63701/HD6303X cycle table (src/devices/cpu/m6800/m6801.cpp: cycles_63701)
const u8 m6800_cpu_device::cycles_63701[256] =
{
		/* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
	/*0*/ 4, 1, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	/*1*/  1, 1, 4, 4, 4, 4, 1, 1, 2, 2, 4, 1, 4, 4, 4, 4,
	/*2*/  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	/*3*/  1, 1, 3, 3, 1, 1, 4, 4, 4, 5, 1,10, 5, 7, 9,12,
	/*4*/  1, 4, 4, 1, 1, 4, 1, 1, 1, 1, 1, 4, 1, 1, 4, 1,
	/*5*/  1, 4, 4, 1, 1, 4, 1, 1, 1, 1, 1, 4, 1, 1, 4, 1,
	/*6*/  6, 7, 7, 6, 6, 7, 6, 6, 6, 6, 6, 5, 6, 4, 3, 5,
	/*7*/  6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 4, 6, 4, 3, 5,
	/*8*/  2, 2, 2, 3, 2, 2, 2, 4, 2, 2, 2, 2, 3, 5, 3, 4,
	/*9*/  3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 5, 4, 4,
	/*A*/  4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
	/*B*/  4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 6, 5, 5,
	/*C*/  2, 2, 2, 3, 2, 2, 2, 4, 2, 2, 2, 2, 3, 4, 3, 4,
	/*D*/  3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,
	/*E*/  4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
	/*F*/  4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5
};

// HD63701/HD6303X opcode table (src/devices/cpu/m6800/m6801.cpp: hd63701_insn)
const m6800_cpu_device::op_func m6800_cpu_device::hd63701_insn[0x100] = {
// 0/8                     1/9                        2/A                        3/B                        4/C                        5/D                        6/E                        7/F
&m6800_cpu_device::trap,   &m6800_cpu_device::nop,    &m6800_cpu_device::trap,   &m6800_cpu_device::trap,   &m6800_cpu_device::lsrd,   &m6800_cpu_device::asld,   &m6800_cpu_device::tap,    &m6800_cpu_device::tpa,    // 0
&m6800_cpu_device::inx,    &m6800_cpu_device::dex,    &m6800_cpu_device::clv,    &m6800_cpu_device::sev,    &m6800_cpu_device::clc,    &m6800_cpu_device::sec,    &m6800_cpu_device::cli,    &m6800_cpu_device::sei,
&m6800_cpu_device::sba,    &m6800_cpu_device::cba,    &m6800_cpu_device::undoc1, &m6800_cpu_device::undoc2, &m6800_cpu_device::trap,   &m6800_cpu_device::trap,   &m6800_cpu_device::tab,    &m6800_cpu_device::tba,    // 1
&m6800_cpu_device::xgdx,   &m6800_cpu_device::daa,    &m6800_cpu_device::slp,    &m6800_cpu_device::aba,    &m6800_cpu_device::trap,   &m6800_cpu_device::trap,   &m6800_cpu_device::trap,   &m6800_cpu_device::trap,
&m6800_cpu_device::bra,    &m6800_cpu_device::brn,    &m6800_cpu_device::bhi,    &m6800_cpu_device::bls,    &m6800_cpu_device::bcc,    &m6800_cpu_device::bcs,    &m6800_cpu_device::bne,    &m6800_cpu_device::beq,    // 2
&m6800_cpu_device::bvc,    &m6800_cpu_device::bvs,    &m6800_cpu_device::bpl,    &m6800_cpu_device::bmi,    &m6800_cpu_device::bge,    &m6800_cpu_device::blt,    &m6800_cpu_device::bgt,    &m6800_cpu_device::ble,
&m6800_cpu_device::tsx,    &m6800_cpu_device::ins,    &m6800_cpu_device::pula,   &m6800_cpu_device::pulb,   &m6800_cpu_device::des,    &m6800_cpu_device::txs,    &m6800_cpu_device::psha,   &m6800_cpu_device::pshb,   // 3
&m6800_cpu_device::pulx,   &m6800_cpu_device::rts,    &m6800_cpu_device::abx,    &m6800_cpu_device::rti,    &m6800_cpu_device::pshx,   &m6800_cpu_device::mul,    &m6800_cpu_device::wai,    &m6800_cpu_device::swi,
&m6800_cpu_device::nega,   &m6800_cpu_device::trap,   &m6800_cpu_device::trap,   &m6800_cpu_device::coma,   &m6800_cpu_device::lsra,   &m6800_cpu_device::trap,   &m6800_cpu_device::rora,   &m6800_cpu_device::asra,   // 4
&m6800_cpu_device::asla,   &m6800_cpu_device::rola,   &m6800_cpu_device::deca,   &m6800_cpu_device::trap,   &m6800_cpu_device::inca,   &m6800_cpu_device::tsta,   &m6800_cpu_device::trap,   &m6800_cpu_device::clra,
&m6800_cpu_device::negb,   &m6800_cpu_device::trap,   &m6800_cpu_device::trap,   &m6800_cpu_device::comb,   &m6800_cpu_device::lsrb,   &m6800_cpu_device::trap,   &m6800_cpu_device::rorb,   &m6800_cpu_device::asrb,   // 5
&m6800_cpu_device::aslb,   &m6800_cpu_device::rolb,   &m6800_cpu_device::decb,   &m6800_cpu_device::trap,   &m6800_cpu_device::incb,   &m6800_cpu_device::tstb,   &m6800_cpu_device::trap,   &m6800_cpu_device::clrb,
&m6800_cpu_device::neg_ix, &m6800_cpu_device::aim_ix, &m6800_cpu_device::oim_ix, &m6800_cpu_device::com_ix, &m6800_cpu_device::lsr_ix, &m6800_cpu_device::eim_ix, &m6800_cpu_device::ror_ix, &m6800_cpu_device::asr_ix, // 6
&m6800_cpu_device::asl_ix, &m6800_cpu_device::rol_ix, &m6800_cpu_device::dec_ix, &m6800_cpu_device::tim_ix, &m6800_cpu_device::inc_ix, &m6800_cpu_device::tst_ix, &m6800_cpu_device::jmp_ix, &m6800_cpu_device::clr_ix,
&m6800_cpu_device::neg_ex, &m6800_cpu_device::aim_di, &m6800_cpu_device::oim_di, &m6800_cpu_device::com_ex, &m6800_cpu_device::lsr_ex, &m6800_cpu_device::eim_di, &m6800_cpu_device::ror_ex, &m6800_cpu_device::asr_ex, // 7
&m6800_cpu_device::asl_ex, &m6800_cpu_device::rol_ex, &m6800_cpu_device::dec_ex, &m6800_cpu_device::tim_di, &m6800_cpu_device::inc_ex, &m6800_cpu_device::tst_ex, &m6800_cpu_device::jmp_ex, &m6800_cpu_device::clr_ex,
&m6800_cpu_device::suba_im,&m6800_cpu_device::cmpa_im,&m6800_cpu_device::sbca_im,&m6800_cpu_device::subd_im,&m6800_cpu_device::anda_im,&m6800_cpu_device::bita_im,&m6800_cpu_device::lda_im, &m6800_cpu_device::trap,   // 8
&m6800_cpu_device::eora_im,&m6800_cpu_device::adca_im,&m6800_cpu_device::ora_im, &m6800_cpu_device::adda_im,&m6800_cpu_device::cpx_im ,&m6800_cpu_device::bsr,    &m6800_cpu_device::lds_im, &m6800_cpu_device::trap,
&m6800_cpu_device::suba_di,&m6800_cpu_device::cmpa_di,&m6800_cpu_device::sbca_di,&m6800_cpu_device::subd_di,&m6800_cpu_device::anda_di,&m6800_cpu_device::bita_di,&m6800_cpu_device::lda_di, &m6800_cpu_device::sta_di, // 9
&m6800_cpu_device::eora_di,&m6800_cpu_device::adca_di,&m6800_cpu_device::ora_di, &m6800_cpu_device::adda_di,&m6800_cpu_device::cpx_di ,&m6800_cpu_device::jsr_di, &m6800_cpu_device::lds_di, &m6800_cpu_device::sts_di,
&m6800_cpu_device::suba_ix,&m6800_cpu_device::cmpa_ix,&m6800_cpu_device::sbca_ix,&m6800_cpu_device::subd_ix,&m6800_cpu_device::anda_ix,&m6800_cpu_device::bita_ix,&m6800_cpu_device::lda_ix, &m6800_cpu_device::sta_ix, // A
&m6800_cpu_device::eora_ix,&m6800_cpu_device::adca_ix,&m6800_cpu_device::ora_ix, &m6800_cpu_device::adda_ix,&m6800_cpu_device::cpx_ix ,&m6800_cpu_device::jsr_ix, &m6800_cpu_device::lds_ix, &m6800_cpu_device::sts_ix,
&m6800_cpu_device::suba_ex,&m6800_cpu_device::cmpa_ex,&m6800_cpu_device::sbca_ex,&m6800_cpu_device::subd_ex,&m6800_cpu_device::anda_ex,&m6800_cpu_device::bita_ex,&m6800_cpu_device::lda_ex, &m6800_cpu_device::sta_ex, // B
&m6800_cpu_device::eora_ex,&m6800_cpu_device::adca_ex,&m6800_cpu_device::ora_ex, &m6800_cpu_device::adda_ex,&m6800_cpu_device::cpx_ex ,&m6800_cpu_device::jsr_ex, &m6800_cpu_device::lds_ex, &m6800_cpu_device::sts_ex,
&m6800_cpu_device::subb_im,&m6800_cpu_device::cmpb_im,&m6800_cpu_device::sbcb_im,&m6800_cpu_device::addd_im,&m6800_cpu_device::andb_im,&m6800_cpu_device::bitb_im,&m6800_cpu_device::ldb_im, &m6800_cpu_device::trap,   // C
&m6800_cpu_device::eorb_im,&m6800_cpu_device::adcb_im,&m6800_cpu_device::orb_im, &m6800_cpu_device::addb_im,&m6800_cpu_device::ldd_im, &m6800_cpu_device::trap,   &m6800_cpu_device::ldx_im, &m6800_cpu_device::trap,
&m6800_cpu_device::subb_di,&m6800_cpu_device::cmpb_di,&m6800_cpu_device::sbcb_di,&m6800_cpu_device::addd_di,&m6800_cpu_device::andb_di,&m6800_cpu_device::bitb_di,&m6800_cpu_device::ldb_di, &m6800_cpu_device::stb_di, // D
&m6800_cpu_device::eorb_di,&m6800_cpu_device::adcb_di,&m6800_cpu_device::orb_di, &m6800_cpu_device::addb_di,&m6800_cpu_device::ldd_di, &m6800_cpu_device::std_di, &m6800_cpu_device::ldx_di, &m6800_cpu_device::stx_di,
&m6800_cpu_device::subb_ix,&m6800_cpu_device::cmpb_ix,&m6800_cpu_device::sbcb_ix,&m6800_cpu_device::addd_ix,&m6800_cpu_device::andb_ix,&m6800_cpu_device::bitb_ix,&m6800_cpu_device::ldb_ix, &m6800_cpu_device::stb_ix, // E
&m6800_cpu_device::eorb_ix,&m6800_cpu_device::adcb_ix,&m6800_cpu_device::orb_ix, &m6800_cpu_device::addb_ix,&m6800_cpu_device::ldd_ix, &m6800_cpu_device::std_ix, &m6800_cpu_device::ldx_ix, &m6800_cpu_device::stx_ix,
&m6800_cpu_device::subb_ex,&m6800_cpu_device::cmpb_ex,&m6800_cpu_device::sbcb_ex,&m6800_cpu_device::addd_ex,&m6800_cpu_device::andb_ex,&m6800_cpu_device::bitb_ex,&m6800_cpu_device::ldb_ex, &m6800_cpu_device::stb_ex, // F
&m6800_cpu_device::eorb_ex,&m6800_cpu_device::adcb_ex,&m6800_cpu_device::orb_ex, &m6800_cpu_device::addb_ex,&m6800_cpu_device::ldd_ex, &m6800_cpu_device::std_ex, &m6800_cpu_device::ldx_ex, &m6800_cpu_device::stx_ex
};

m6800_cpu_device::m6800_cpu_device(tx81z::Bus &bus)
	: m_bus(bus)
{
	m_insn = hd63701_insn;
	m_cycles = cycles_63701;
}

u32 m6800_cpu_device::RM16(u32 Addr)
{
	u32 result = RM(Addr) << 8;
	return result | RM((Addr + 1) & 0xffff);
}

void m6800_cpu_device::WM16(u32 Addr, PAIR *p)
{
	WM(Addr, p->b.h);
	WM((Addr + 1) & 0xffff, p->b.l);
}

void m6800_cpu_device::enter_interrupt(const char *message, u16 irq_vector)
{
	int cycles_to_eat;

	if (m_wai_state & M6800_WAI)
	{
		cycles_to_eat = 4;
		m_wai_state &= ~M6800_WAI;
	}
	else
	{
		PUSHWORD(pPC);
		PUSHWORD(pX);
		PUSHBYTE(A);
		PUSHBYTE(B);
		PUSHBYTE(CC);
		cycles_to_eat = 12;
	}
	SEI;
	PCD = RM16(irq_vector);

	increment_counter(cycles_to_eat);
}

void m6800_cpu_device::check_irq_lines()
{
	if (m_nmi_pending)
	{
		m_wai_state &= ~M6800_SLP;
		m_nmi_pending = false;
		enter_interrupt("NMI", 0xfffc);
	}
	else if (check_irq1_enabled())
	{
		m_wai_state &= ~M6800_SLP;

		if (!(CC & 0x10))
		{
			enter_interrupt("IRQ1", 0xfff8);
		}
	}
	else
		check_irq2();
}

// HD6301X gates IRQ1 recognition behind RAM Control Register bit 0
// (src/devices/cpu/m6800/m6801.cpp: hd6301x_cpu_device::check_irq1_enabled)
bool m6800_cpu_device::check_irq1_enabled()
{
	return m_irq_state[0] != 0 && (m_ram_ctrl & 1) != 0;
}

//
// ===== On-chip peripherals (timer, SCI/UART, ports 2/5/6) =====
// Ported from src/devices/cpu/m6800/m6801.cpp - m6801_cpu_device for the
// generic parts, hd6301x_cpu_device overrides where TX81Z's HD6303X differs.
// Only registers actually mapped by hd6303x_io/hd6303x_mem are implemented -
// ports 1/3/4/7 are absent for this chip (those pins are the external bus).
//

// Timer Control/Status Register bits
#define TCSR_OLVL   0x01
#define TCSR_IEDG   0x02
#define TCSR_ETOI   0x04
#define TCSR_EOCI   0x08
#define TCSR_EICI   0x10
#define TCSR_TOF    0x20
#define TCSR_OCF    0x40
#define TCSR_ICF    0x80

// hd6301x's second Timer Control/Status Register bits
#define TCSR2_OE1   0x01
#define TCSR2_OE2   0x02
#define TCSR2_OLVL2 0x04
#define TCSR2_EOCI2 0x08
#define TCSR2_OCF2  0x20

// SCI Transmit/Receive Control and Status Register bits
#define M6801_TRCSR_RDRF 0x80
#define M6801_TRCSR_ORFE 0x40
#define M6801_TRCSR_TDRE 0x20
#define M6801_TRCSR_RIE  0x10
#define M6801_TRCSR_RE   0x08
#define M6801_TRCSR_TIE  0x04
#define M6801_TRCSR_TE   0x02
#define M6801_TRCSR_WU   0x01

#define M6801_SERIAL_START 0
#define M6801_SERIAL_STOP  9
#define M6801_TX_STATE_INIT   0
#define M6801_TX_STATE_READY  1

#define CT   m_counter.w.l
#define CTH  m_counter.w.h
#define CTD  m_counter.d
#define OC   m_output_compare[0].w.l
#define OCH  m_output_compare[0].w.h
#define OCD  m_output_compare[0].d
#define OC2  m_output_compare[1].w.l
#define OC2H m_output_compare[1].w.h
#define OC2D m_output_compare[1].d
#define TOH  m_timer_over.w.h
#define TOD  m_timer_over.d

void m6800_cpu_device::modified_counters()
{
	OCH = (OC >= CT) ? CTH : CTH + 1;
	OC2H = (OC2 >= CT) ? CTH : CTH + 1;
	set_timer_event();
}

void m6800_cpu_device::set_timer_event()
{
	m_timer_next = (OCD < TOD) ? OCD : TOD;
	if (OC2D < m_timer_next)
		m_timer_next = OC2D;
}

void m6800_cpu_device::check_timer_event()
{
	if (CTD >= OCD)
	{
		OCH++;
		m_tcsr |= TCSR_OCF;
		m_pending_tcsr |= TCSR_OCF;
		// port2 bit1 output-compare toggle (P21) - inert for TX81Z (unwired)
	}
	if (CTD >= OC2D)
	{
		OC2H++;
		m_tcsr2 |= TCSR2_OCF2;
		m_pending_tcsr2 |= TCSR2_OCF2;
		// port2 bit5 output-compare toggle (P25) - inert for TX81Z (unwired)
	}
	if (CTD >= TOD)
	{
		TOH++;
		m_tcsr |= TCSR_TOF;
		m_pending_tcsr |= TCSR_TOF;
	}

	check_irq2();
	set_timer_event();
}

void m6800_cpu_device::increment_counter(int amount)
{
	m_icount -= amount;

	if (m_t2cnt_written)
	{
		m_t2cnt_written = false;
	}
	else if (m_tcsr3 & 0x10)
	{
		switch (m_tcsr3 & 0x03)
		{
		case 0x00: increment_t2cnt(amount); break;
		case 0x01: increment_t2cnt((amount + (CTD & 0x0007)) >> 3); break;
		case 0x02: increment_t2cnt((amount + (CTD & 0x007f)) >> 7); break;
		case 0x03: break; // external Tclk - not driven in this engine
		}
	}

	CTD += amount;
	if (CTD >= m_timer_next || (m_tcsr3 & 0xc0) == 0xc0)
		check_timer_event();
}

void m6800_cpu_device::eat_cycles()
{
	if (m_tcsr3 & 0x10)
	{
		while (m_icount > 0 && (m_wai_state & (M6800_WAI | M6800_SLP)))
			increment_counter(1);
	}
	else
	{
		int cycles_to_eat = (int(m_timer_next - CTD) < m_icount) ? int(m_timer_next - CTD) : m_icount;
		if (cycles_to_eat > 0)
			increment_counter(cycles_to_eat);
	}
}

void m6800_cpu_device::cleanup_counters()
{
	OC2H -= CTH;
	OCH -= CTH;
	TOH -= CTH;
	CTH = 0;
	set_timer_event();
	if (CTD >= m_timer_next)
		check_timer_event();
}

void m6800_cpu_device::take_trap()
{
	enter_interrupt("TRAP", 0xffee);
}

void m6800_cpu_device::check_irq2()
{
	bool ici = (m_tcsr & (TCSR_EICI | TCSR_ICF)) == (TCSR_EICI | TCSR_ICF);
	bool oci = (m_tcsr & (TCSR_EOCI | TCSR_OCF)) == (TCSR_EOCI | TCSR_OCF);
	bool oci2 = (m_tcsr2 & (TCSR2_EOCI2 | TCSR2_OCF2)) == (TCSR2_EOCI2 | TCSR2_OCF2);
	bool toi = (m_tcsr & (TCSR_ETOI | TCSR_TOF)) == (TCSR_ETOI | TCSR_TOF);
	bool sci = ((m_trcsr & (M6801_TRCSR_RIE | M6801_TRCSR_RDRF)) == (M6801_TRCSR_RIE | M6801_TRCSR_RDRF)) ||
	           ((m_trcsr & (M6801_TRCSR_RIE | M6801_TRCSR_ORFE)) == (M6801_TRCSR_RIE | M6801_TRCSR_ORFE)) ||
	           ((m_trcsr & (M6801_TRCSR_TIE | M6801_TRCSR_TDRE)) == (M6801_TRCSR_TIE | M6801_TRCSR_TDRE));

	auto take = [this](u16 vector)
	{
		m_wai_state &= ~M6800_SLP;
		if (!(m_cc & 0x10))
			enter_interrupt("IRQ2", vector);
	};

	if (ici)
		take(0xfff6);
	else if (oci || oci2)
		take(0xfff4);
	else if (toi)
		take(0xfff2);
	else if ((m_tcsr3 & 0xc0) == 0xc0)
		take(0xffec); // CMI (timer 2 compare match)
	else if (m_irq_state[4] != 0 && (m_ram_ctrl & 2)) // HD6301_IRQ2_LINE - unused by TX81Z
		take(0xffea);
	else if (sci)
		take(0xfff0);
}

// --- port2 (0x0001 ddr [2-bit], 0x0003 data) ---

u8 m6800_cpu_device::p2_data_r()
{
	u8 ddr = m_port2_ddr;
	u8 in = port2_input ? port2_input() : 0xff;
	return (in & ~ddr) | (m_port2_data & ddr);
}

void m6800_cpu_device::p2_data_w(u8 data)
{
	m_port2_data = data;
	m_port2_written = true;
}

void m6800_cpu_device::p2_ddr_w(u8 data)
{
	// HD6301X's DDR2 register is only 2 raw bits: bit0 controls P20's
	// direction directly, bit1 gates the rest of the port (P21-P27) as one
	// block - see hd6301x_cpu_device::p2_ddr_2bit_w in MAME's m6801.cpp.
	u8 effective = (data & 0x02 ? 0xfe : 0x00) | (data & 0x01);
	m_port2_ddr = effective;
}

void m6800_cpu_device::write_port2()
{
	// No-op: ymtx81z.cpp never wires an out_p2_cb(), so port2's output pins
	// aren't connected to anything on the real TX81Z board. The externally
	// observable effect that matters (MIDI Out) comes from serial_tx_output()
	// in serial_transmit(), not from this latch.
}

// --- port6 (0x0016 ddr, 0x0017 data) - ROM bank select + LEDs are outputs ---

u8 m6800_cpu_device::p6_data_r()
{
	u8 ddr = m_port6_ddr;
	if (ddr == 0xff)
		return m_port6_data;
	u8 in = port6_input ? port6_input() : 0xff;
	return (in & ~ddr) | (m_port6_data & ddr);
}

void m6800_cpu_device::p6_data_w(u8 data)
{
	m_port6_data = data;
	if (port6_output)
		port6_output((m_port6_data & m_port6_ddr) | (m_port6_ddr ^ 0xff), m_port6_ddr);
}

void m6800_cpu_device::p6_ddr_w(u8 data)
{
	if (m_port6_ddr != data)
	{
		m_port6_ddr = data;
		if (port6_output)
			port6_output((m_port6_data & m_port6_ddr) | (m_port6_ddr ^ 0xff), m_port6_ddr);
	}
}

// --- port5 (0x0015, read-only) ---

u8 m6800_cpu_device::p5_data_r()
{
	u8 data = port5_input ? port5_input() : 0xff;
	if (m_irq_state[0]) data &= 0xfe;  // HD6301_IRQ1_LINE reflected on P50
	if (m_irq_state[4]) data &= 0xfd;  // HD6301_IRQ2_LINE reflected on P51 (unused by TX81Z)
	return data;
}

// --- timer registers ---

u8 m6800_cpu_device::tcsr_r()
{
	m_pending_tcsr = 0;
	return m_tcsr;
}

void m6800_cpu_device::tcsr_w(u8 data)
{
	data &= 0x1f;
	m_tcsr = data | (m_tcsr & 0xe0);
	m_pending_tcsr &= m_tcsr;
	check_irq2();
}

u8 m6800_cpu_device::ch_r()
{
	if (!(m_pending_tcsr & TCSR_TOF))
		m_tcsr &= ~TCSR_TOF;
	return m_counter.b.h;
}

u8 m6800_cpu_device::cl_r()
{
	return m_counter.b.l;
}

void m6800_cpu_device::ch_w(u8 data)
{
	m_latch09 = data;
	CT = 0xfff8;
	TOH = CTH;
	modified_counters();
}

void m6800_cpu_device::cl_w(u8 data)
{
	CT = (m_latch09 << 8) | data;
	TOH = CTH;
	modified_counters();
}

u8 m6800_cpu_device::ocrh_r() { return m_output_compare[0].b.h; }
u8 m6800_cpu_device::ocrl_r() { return m_output_compare[0].b.l; }

void m6800_cpu_device::ocrh_w(u8 data)
{
	if (!(m_pending_tcsr & TCSR_OCF))
		m_tcsr &= ~TCSR_OCF;
	if (m_output_compare[0].b.h != data)
	{
		m_output_compare[0].b.h = data;
		modified_counters();
	}
}

void m6800_cpu_device::ocrl_w(u8 data)
{
	if (!(m_pending_tcsr & TCSR_OCF))
		m_tcsr &= ~TCSR_OCF;
	if (m_output_compare[0].b.l != data)
	{
		m_output_compare[0].b.l = data;
		modified_counters();
	}
}

u8 m6800_cpu_device::icrh_r()
{
	if (!(m_pending_tcsr & TCSR_ICF))
		m_tcsr &= ~TCSR_ICF;
	return (m_input_capture >> 8) & 0xff;
}

u8 m6800_cpu_device::icrl_r()
{
	return m_input_capture & 0xff;
}

u8 m6800_cpu_device::tcsr2_r()
{
	m_pending_tcsr &= ~(TCSR_ICF | TCSR_OCF);
	m_pending_tcsr2 = 0;
	return m_tcsr2 | (m_tcsr & (TCSR_ICF | TCSR_OCF)) | 0x10;
}

void m6800_cpu_device::tcsr2_w(u8 data)
{
	data &= (TCSR2_OE1 | TCSR2_OE2 | TCSR2_OLVL2 | TCSR2_EOCI2);
	m_tcsr2 = data | (m_tcsr2 & TCSR2_OCF2);
	m_pending_tcsr2 &= m_tcsr2;
	check_irq2();
}

u8 m6800_cpu_device::ocr2h_r() { return m_output_compare[1].b.h; }
u8 m6800_cpu_device::ocr2l_r() { return m_output_compare[1].b.l; }

void m6800_cpu_device::ocr2h_w(u8 data)
{
	if (!(m_pending_tcsr2 & TCSR2_OCF2))
		m_tcsr2 &= ~TCSR2_OCF2;
	if (m_output_compare[1].b.h != data)
	{
		m_output_compare[1].b.h = data;
		modified_counters();
	}
}

void m6800_cpu_device::ocr2l_w(u8 data)
{
	if (!(m_pending_tcsr2 & TCSR2_OCF2))
		m_tcsr2 &= ~TCSR2_OCF2;
	if (m_output_compare[1].b.l != data)
	{
		m_output_compare[1].b.l = data;
		modified_counters();
	}
}

// --- timer 2 (dedicated sub-timer, drives TOUT3 / can drive SCI baud) ---

void m6800_cpu_device::increment_t2cnt(int amount)
{
	while (amount--)
	{
		if (++m_t2cnt == ((m_tconr + 1) & 0xff))
		{
			m_t2cnt = 0;

			if (m_tcsr3 & 0x08)
			{
				bool level = (m_tcsr3 & 0x04) != 0;
				if (m_tout3 != level)
					m_tout3 = level;
			}
			else if (m_tcsr3 & 0x04)
			{
				m_tout3 = !m_tout3;
			}

			if ((m_rmcr & 0x20) && !m_use_ext_serclock)
				sci_clock_internal(32);

			m_tcsr3 |= 0x80;
			m_timer_next = 0; // matches MAME's HACK comment - forces a re-check
		}
	}
}

u8 m6800_cpu_device::t2cnt_r() { return m_t2cnt; }
void m6800_cpu_device::t2cnt_w(u8 data) { m_t2cnt = data; m_t2cnt_written = true; }
void m6800_cpu_device::tconr_w(u8 data) { m_tconr = data; }
u8 m6800_cpu_device::tcsr3_r() { return m_tcsr3; }

void m6800_cpu_device::tcsr3_w(u8 data)
{
	m_tcsr3 = data & (0x5f | (m_tcsr3 & 0x80));

	if (m_tout3 && !(data & 0x10))
		m_tout3 = false;

	check_irq2();
}

// --- SCI / UART ---

u8 m6800_cpu_device::sci_rmcr_r() { return m_rmcr; }
void m6800_cpu_device::sci_rmcr_w(u8 data) { set_rmcr(data); }

u8 m6800_cpu_device::sci_trcsr_r()
{
	if (m_trcsr & M6801_TRCSR_TDRE) m_trcsr_read_tdre = 1;
	if (m_trcsr & M6801_TRCSR_ORFE) m_trcsr_read_orfe = 1;
	if (m_trcsr & M6801_TRCSR_RDRF) m_trcsr_read_rdrf = 1;
	return m_trcsr;
}

void m6800_cpu_device::sci_trcsr_w(u8 data)
{
	if ((data & M6801_TRCSR_TE) && !(m_trcsr & M6801_TRCSR_TE))
	{
		m_txstate = M6801_TX_STATE_INIT;
		m_txbits = 0;
		m_tx = 1;
	}
	if ((data & M6801_TRCSR_RE) && !(m_trcsr & M6801_TRCSR_RE))
	{
		m_rxbits = 0;
	}
	m_trcsr = (m_trcsr & 0xe0) | (data & 0x1f);
}

u8 m6800_cpu_device::sci_rdr_r()
{
	if (m_trcsr_read_orfe) { m_trcsr_read_orfe = 0; m_trcsr &= ~M6801_TRCSR_ORFE; }
	if (m_trcsr_read_rdrf) { m_trcsr_read_rdrf = 0; m_trcsr &= ~M6801_TRCSR_RDRF; }
	return m_rdr;
}

void m6800_cpu_device::sci_tdr_w(u8 data)
{
	if (m_trcsr_read_tdre) { m_trcsr_read_tdre = 0; m_trcsr &= ~M6801_TRCSR_TDRE; }
	m_tdr = data;
}

void m6800_cpu_device::set_rmcr(u8 data)
{
	if (m_rmcr == data)
		return;
	m_rmcr = data;

	switch ((m_rmcr & 0x1c) >> 2)
	{
	case 0: case 3: case 7: // external clock (what the real TX81Z always uses)
		reset_sci_timer();
		m_use_ext_serclock = true;
		break;
	default:
		// Internal-baud-rate-generator mode: real hardware derives the SCI
		// clock from the on-chip timer/E-clock divider here. ymtx81z.cpp
		// always wires the external 500kHz clock path above, so this mode
		// isn't implemented - firmware on the real board never selects it.
		reset_sci_timer();
		m_use_ext_serclock = false;
		break;
	}
}

int m6800_cpu_device::m6801_rx()
{
	u8 in = port2_input ? port2_input() : 0xff;
	return (in & 0x08) >> 3;
}

void m6800_cpu_device::serial_transmit()
{
	if (!(m_trcsr & M6801_TRCSR_TE))
		return;

	int old_tx = m_tx;

	switch (m_txstate)
	{
	case M6801_TX_STATE_INIT:
		m_tx = 1;
		m_txbits++;
		if (m_txbits == 10)
		{
			m_txstate = M6801_TX_STATE_READY;
			m_txbits = M6801_SERIAL_START;
		}
		break;

	case M6801_TX_STATE_READY:
		switch (m_txbits)
		{
		case M6801_SERIAL_START:
			if (m_trcsr & M6801_TRCSR_TDRE)
				return; // nothing to send
			m_tshr = m_tdr;
			m_trcsr |= M6801_TRCSR_TDRE;
			m_tx = 0; // start bit
			m_txbits++;
			break;

		case M6801_SERIAL_STOP:
			m_tx = 1; // stop bit
			check_irq_lines();
			m_txbits = M6801_SERIAL_START;
			break;

		default:
			m_tx = m_tshr & 0x01;
			m_tshr >>= 1;
			m_txbits++;
			break;
		}
		break;
	}

	if (old_tx != m_tx && serial_tx_output)
		serial_tx_output(m_tx == 1);
}

void m6800_cpu_device::serial_receive()
{
	if (!(m_trcsr & M6801_TRCSR_RE))
		return;

	if (m_trcsr & M6801_TRCSR_WU)
	{
		if (m6801_rx() == 1)
		{
			m_rxbits++;
			if (m_rxbits == 10)
			{
				m_trcsr &= ~M6801_TRCSR_WU;
				m_rxbits = M6801_SERIAL_START;
			}
		}
		else
		{
			m_rxbits = M6801_SERIAL_START;
		}
		return;
	}

	switch (m_rxbits)
	{
	case M6801_SERIAL_START:
		if (m6801_rx() == 0)
			m_rxbits++;
		break;

	case M6801_SERIAL_STOP:
		if (m6801_rx() == 1)
		{
			if (m_trcsr & M6801_TRCSR_RDRF)
			{
				m_trcsr |= M6801_TRCSR_ORFE;
				if (on_sci_error) on_sci_error(1);
				check_irq_lines();
			}
			else if (!(m_trcsr & M6801_TRCSR_ORFE))
			{
				m_rdr = m_rsr;
				m_trcsr |= M6801_TRCSR_RDRF;
				if (on_sci_byte_received)
					on_sci_byte_received(m_rdr);
				check_irq_lines();
			}
		}
		else
		{
			if (!(m_trcsr & M6801_TRCSR_ORFE))
				m_rdr = m_rsr;
			m_trcsr |= M6801_TRCSR_ORFE;
			m_trcsr &= ~M6801_TRCSR_RDRF;
			if (on_sci_error) on_sci_error(2);
			check_irq_lines();
		}
		m_rxbits = M6801_SERIAL_START;
		break;

	default:
		m_rsr >>= 1;
		m_rsr |= (m6801_rx() << 7);
		m_rxbits++;
		break;
	}
}

void m6800_cpu_device::sci_tick()
{
	serial_transmit();
	serial_receive();
}

void m6800_cpu_device::sci_clock_internal(u8 divider)
{
	m_sci_clocks++;
	if (m_sci_clocks >= divider)
	{
		m_sci_clocks = 0;
		sci_tick();
	}
}

void m6800_cpu_device::clock_serial()
{
	if (m_use_ext_serclock)
		sci_clock_internal(m_sclk_divider);
}

void m6800_cpu_device::reset_sci_timer()
{
	m_sci_clocks = 0;
}

// --- RAM control register (0x0014) ---

u8 m6800_cpu_device::rcr_r() { return m_ram_ctrl | 0x30; }
void m6800_cpu_device::rcr_w(u8 data) { m_ram_ctrl = data; check_irq_lines(); }

// --- internal address space dispatch (0x0000-0x00FF) ---

u8 m6800_cpu_device::internal_read(u16 addr)
{
	switch (addr)
	{
	case 0x0001: return 0xff; // p2 ddr is write-only
	case 0x0003: return p2_data_r();
	case 0x0008: return tcsr_r();
	case 0x0009: return ch_r();
	case 0x000a: return cl_r();
	case 0x000b: return ocrh_r();
	case 0x000c: return ocrl_r();
	case 0x000d: return icrh_r();
	case 0x000e: return icrl_r();
	case 0x000f: return tcsr2_r();
	case 0x0010: return sci_rmcr_r();
	case 0x0011: return sci_trcsr_r();
	case 0x0012: return sci_rdr_r();
	case 0x0014: return rcr_r();
	case 0x0015: return p5_data_r();
	case 0x0016: return 0xff; // p6 ddr is write-only
	case 0x0017: return p6_data_r();
	case 0x0019: return ocr2h_r();
	case 0x001a: return ocr2l_r();
	case 0x001b: return tcsr3_r();
	case 0x001c: return 0xff; // tconr is write-only
	case 0x001d: return t2cnt_r();
	default:
		if (addr >= 0x0040)
			return m_internal_ram[addr - 0x0040];
		return 0xff; // unmapped
	}
}

void m6800_cpu_device::internal_write(u16 addr, u8 data)
{
	switch (addr)
	{
	case 0x0001: p2_ddr_w(data); return;
	case 0x0003: p2_data_w(data); return;
	case 0x0008: tcsr_w(data); return;
	case 0x0009: ch_w(data); return;
	case 0x000a: cl_w(data); return;
	case 0x000b: ocrh_w(data); return;
	case 0x000c: ocrl_w(data); return;
	case 0x000f: tcsr2_w(data); return;
	case 0x0010: sci_rmcr_w(data); return;
	case 0x0011: sci_trcsr_w(data); return;
	case 0x0013: sci_tdr_w(data); return;
	case 0x0014: rcr_w(data); return;
	case 0x0016: p6_ddr_w(data); return;
	case 0x0017: p6_data_w(data); return;
	case 0x0019: ocr2h_w(data); return;
	case 0x001a: ocr2l_w(data); return;
	case 0x001b: tcsr3_w(data); return;
	case 0x001c: tconr_w(data); return;
	case 0x001d: t2cnt_w(data); return;
	default:
		if (addr >= 0x0040)
			m_internal_ram[addr - 0x0040] = data;
		return; // unmapped, ignored
	}
}

void m6800_cpu_device::reset()
{
	m_ppc.d = 0;
	m_pc.d = 0;
	m_s.d = 0;
	m_x.d = 0;
	m_d.d = 0;
	m_cc = 0xc0;
	SEI; /* IRQ disabled */
	PCD = RM16(0xfffe);

	m_wai_state = 0;
	m_nmi_state = 0;
	m_nmi_pending = 0;

	// peripheral reset (m6801_cpu_device::device_reset + hd6301x_cpu_device::device_reset)
	m_port2_ddr = 0; m_port2_data = 0; m_port2_written = false;
	m_port6_ddr = 0; m_port6_data = 0;

	m_tcsr = 0; m_pending_tcsr = 0;
	m_counter.d = 0;
	m_output_compare[0].d = 0xffff;
	m_timer_over.d = 0xffff;
	m_timer_next = 0xffff;
	m_latch09 = 0;
	m_input_capture = 0;

	m_tcsr2 = 0; m_pending_tcsr2 = 0;
	m_output_compare[1].d = 0xffff;

	m_t2cnt = 0; m_tconr = 0xff; m_tcsr3 = 0; m_tout3 = false; m_t2cnt_written = false;

	m_trcsr = M6801_TRCSR_TDRE;
	m_txstate = M6801_TX_STATE_INIT;
	m_txbits = 0; m_rxbits = 0; m_tx = 1;
	m_trcsr_read_tdre = 0; m_trcsr_read_orfe = 0; m_trcsr_read_rdrf = 0;
	m_rmcr = 0;
	m_use_ext_serclock = true; // rmcr==0 selects the external-clock case
	reset_sci_timer();

	m_ram_ctrl = 0x7c; // bit0 (IRQ1 gate) and bit1 start cleared - firmware must enable them
}

void m6800_cpu_device::set_irq1(bool asserted)
{
	m_irq_state[0] = asserted ? 1 : 0;
}

void m6800_cpu_device::set_nmi(bool asserted)
{
	if (!m_nmi_state && asserted)
		m_nmi_pending = true;
	m_nmi_state = asserted ? 1 : 0;
}

void m6800_cpu_device::execute_one()
{
	pPPC = pPC;
	u8 ireg = M_RDOP(PCD);
	PC++;
	(this->*m_insn[ireg])();
	increment_counter(m_cycles[ireg]);
}

int m6800_cpu_device::run(int cycles)
{
	m_icount = cycles;

	check_irq_lines();
	cleanup_counters();

	do
	{
		if (m_wai_state & (M6800_WAI | M6800_SLP))
		{
			eat_cycles();
		}
		else
		{
			execute_one();
		}
	} while (m_icount > 0);

	return cycles - m_icount;
}
