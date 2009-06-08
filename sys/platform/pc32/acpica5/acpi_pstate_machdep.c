/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/globaldata.h>

#include <machine/md_var.h>
#include <machine/cpufunc.h>
#include <machine/cpufreq.h>

#include "acpi.h"
#include "acpi_cpu_pstate.h"

#define AMD_APMI_HWPSTATE		0x80

#define AMD_MSR_PSTATE_CSR_MASK		0x7ULL
#define AMD1XH_MSR_PSTATE_CTL		0xc0010062
#define AMD1XH_MSR_PSTATE_ST		0xc0010063

#define AMD_MSR_PSTATE_EN		0x8000000000000000ULL

#define AMD10H_MSR_PSTATE_START		0xc0010064
#define AMD10H_MSR_PSTATE_COUNT		5

#define AMD0F_PST_CTL_FID(cval)		(((cval) >> 0)  & 0x3f)
#define AMD0F_PST_CTL_VID(cval)		(((cval) >> 6)  & 0x1f)
#define AMD0F_PST_CTL_VST(cval)		(((cval) >> 11) & 0x7f)
#define AMD0F_PST_CTL_MVS(cval)		(((cval) >> 18) & 0x3)
#define AMD0F_PST_CTL_PLLTIME(cval)	(((cval) >> 20) & 0x7f)
#define AMD0F_PST_CTL_RVO(cval)		(((cval) >> 28) & 0x3)
#define AMD0F_PST_CTL_IRT(cval)		(((cval) >> 30) & 0x3)

#define AMD0F_PST_ST_FID(sval)		(((sval) >> 0) & 0x3f)
#define AMD0F_PST_ST_VID(sval)		(((sval) >> 6) & 0x3f)

static const struct acpi_pst_md *
		acpi_pst_amd_probe(void);
static int	acpi_pst_amd_check_csr(const ACPI_RESOURCE_GENERIC_REGISTER *,
		    const ACPI_RESOURCE_GENERIC_REGISTER *);
static int	acpi_pst_amd1x_check_pstates(const struct acpi_pstate *, int,
		    uint32_t, uint32_t);
static int	acpi_pst_amd10_check_pstates(const struct acpi_pstate *, int);
static int	acpi_pst_amd0f_check_pstates(const struct acpi_pstate *, int);
static int	acpi_pst_amd1x_set_pstate(
		    const ACPI_RESOURCE_GENERIC_REGISTER *,
		    const ACPI_RESOURCE_GENERIC_REGISTER *,
		    const struct acpi_pstate *);
static int	acpi_pst_amd0f_set_pstate(
		    const ACPI_RESOURCE_GENERIC_REGISTER *,
		    const ACPI_RESOURCE_GENERIC_REGISTER *,
		    const struct acpi_pstate *);
static const struct acpi_pstate *
		acpi_pst_amd1x_get_pstate(
		    const ACPI_RESOURCE_GENERIC_REGISTER *,
		    const struct acpi_pstate *, int);
static const struct acpi_pstate *
		acpi_pst_amd0f_get_pstate(
		    const ACPI_RESOURCE_GENERIC_REGISTER *,
		    const struct acpi_pstate *, int);

static const struct acpi_pst_md	acpi_pst_amd10h = {
	.pmd_check_csr		= acpi_pst_amd_check_csr,
	.pmd_check_pstates	= acpi_pst_amd10_check_pstates,
	.pmd_set_pstate		= acpi_pst_amd1x_set_pstate,
	.pmd_get_pstate		= acpi_pst_amd1x_get_pstate
};

static const struct acpi_pst_md	acpi_pst_amd0fh = {
	.pmd_check_csr		= acpi_pst_amd_check_csr,
	.pmd_check_pstates	= acpi_pst_amd0f_check_pstates,
	.pmd_set_pstate		= acpi_pst_amd0f_set_pstate,
	.pmd_get_pstate		= acpi_pst_amd0f_get_pstate
};

const struct acpi_pst_md *
acpi_pst_md_probe(void)
{
	if (strcmp(cpu_vendor, "AuthenticAMD") == 0)
		return acpi_pst_amd_probe();
	return NULL;
}

static const struct acpi_pst_md *
acpi_pst_amd_probe(void)
{
	uint32_t regs[4], ext_family;

	if ((cpu_id & 0x00000f00) != 0x00000f00)
		return NULL;

	/* Check whether APMI exists */
	do_cpuid(0x80000000, regs);
	if (regs[0] < 0x80000007)
		return NULL;

	/* Fetch APMI */
	do_cpuid(0x80000007, regs);

	ext_family = cpu_id & 0x0ff00000;
	switch (ext_family) {
	case 0x00000000:	/* Family 0fh */
		if ((regs[3] & 0x06) == 0x06)
			return &acpi_pst_amd0fh;
		break;

	case 0x00100000:	/* Family 10h */
		if (regs[3] & 0x80)
			return &acpi_pst_amd10h;
		break;

	default:
		break;
	}
	return NULL;
}

static int
acpi_pst_amd_check_csr(const ACPI_RESOURCE_GENERIC_REGISTER *ctrl,
		       const ACPI_RESOURCE_GENERIC_REGISTER *status)
{
	if (ctrl->SpaceId != ACPI_ADR_SPACE_FIXED_HARDWARE) {
		kprintf("cpu%d: Invalid P-State control register\n", mycpuid);
		return EINVAL;
	}
	if (status->SpaceId != ACPI_ADR_SPACE_FIXED_HARDWARE) {
		kprintf("cpu%d: Invalid P-State status register\n", mycpuid);
		return EINVAL;
	}
	return 0;
}

static int
acpi_pst_amd1x_check_pstates(const struct acpi_pstate *pstates, int npstates,
			     uint32_t msr_start, uint32_t msr_end)
{
	int i;

	/*
	 * Make sure that related MSR P-State registers are enabled.
	 *
	 * NOTE:
	 * We don't check status register value here;
	 * it will not be used.
	 */
	for (i = 0; i < npstates; ++i) {
		uint64_t pstate;
		uint32_t msr;

		msr = msr_start +
		      (pstates[i].st_cval & AMD_MSR_PSTATE_CSR_MASK);
		if (msr >= msr_end) {
			kprintf("cpu%d: MSR P-State register %#08x "
				"does not exist\n", mycpuid, msr);
			return EINVAL;
		}

		pstate = rdmsr(msr);
		if ((pstate & AMD_MSR_PSTATE_EN) == 0) {
			kprintf("cpu%d: MSR P-State register %#08x "
				"is not enabled\n", mycpuid, msr);
			return EINVAL;
		}
	}
	return 0;
}

static int
acpi_pst_amd10_check_pstates(const struct acpi_pstate *pstates, int npstates)
{
	/* Only P0-P4 are supported */
	if (npstates > AMD10H_MSR_PSTATE_COUNT) {
		kprintf("cpu%d: only P0-P4 is allowed\n", mycpuid);
		return EINVAL;
	}

	return acpi_pst_amd1x_check_pstates(pstates, npstates,
			AMD10H_MSR_PSTATE_START,
			AMD10H_MSR_PSTATE_START + AMD10H_MSR_PSTATE_COUNT);
}

static int
acpi_pst_amd1x_set_pstate(const ACPI_RESOURCE_GENERIC_REGISTER *ctrl __unused,
			  const ACPI_RESOURCE_GENERIC_REGISTER *status __unused,
			  const struct acpi_pstate *pstate)
{
	uint64_t cval;

	cval = pstate->st_cval & AMD_MSR_PSTATE_CSR_MASK;
	wrmsr(AMD1XH_MSR_PSTATE_CTL, cval);

	/*
	 * Don't check AMD1XH_MSR_PSTATE_ST here, since it is
	 * affected by various P-State limits.
	 *
	 * For details:
	 * AMD Family 10h Processor BKDG Rev 3.20 (#31116)
	 * 2.4.2.4 P-state Transition Behavior
	 */

	return 0;
}

static const struct acpi_pstate *
acpi_pst_amd1x_get_pstate(const ACPI_RESOURCE_GENERIC_REGISTER *status __unused,
			  const struct acpi_pstate *pstates, int npstates)
{
	uint64_t sval;
	int i;

	sval = rdmsr(AMD1XH_MSR_PSTATE_ST) & AMD_MSR_PSTATE_CSR_MASK;
	for (i = 0; i < npstates; ++i) {
		if ((pstates[i].st_sval & AMD_MSR_PSTATE_CSR_MASK) == sval)
			return &pstates[i];
	}
	return NULL;
}

static int
acpi_pst_amd0f_check_pstates(const struct acpi_pstate *pstates, int npstates)
{
	struct amd0f_fidvid fv_max, fv_min;
	int i;

	amd0f_fidvid_limit(&fv_min, &fv_max);

	for (i = 0; i < npstates; ++i) {
		const struct acpi_pstate *p = &pstates[i];
		uint32_t fid, vid, mvs, rvo;
		int mvs_mv, rvo_mv;

		fid = AMD0F_PST_CTL_FID(p->st_cval);
		vid = AMD0F_PST_CTL_VID(p->st_cval);

		if (fid > fv_max.fid || fid < fv_min.fid) {
			kprintf("cpu%d: Invalid FID %#x [%#x, %#x]\n",
				mycpuid, fid, fv_min.fid, fv_max.fid);
			return EINVAL;
		}
		if (vid < fv_max.vid || vid > fv_min.vid) {
			kprintf("cpu%d: Invalid VID %#x [%#x, %#x]\n",
				mycpuid, vid, fv_max.vid, fv_min.fid);
			return EINVAL;
		}

		mvs = AMD0F_PST_CTL_MVS(p->st_cval);
		rvo = AMD0F_PST_CTL_RVO(p->st_cval);

		/* Only 0 is allowed, i.e. 25mV stepping */
		if (mvs != 0) {
			kprintf("cpu%d: Invalid MVS %#x\n", mycpuid, mvs);
			return EINVAL;
		}

		/* -> mV */
		mvs_mv = 25 * (1 << mvs);
		rvo_mv = 25 * rvo;
		if (rvo_mv % mvs_mv != 0) {
			kprintf("cpu%d: Invalid MVS/RVO (%#x/%#x)\n",
				mycpuid, mvs, rvo);
			return EINVAL;
		}
	}
	return 0;
}

static int
acpi_pst_amd0f_set_pstate(const ACPI_RESOURCE_GENERIC_REGISTER *ctrl __unused,
			  const ACPI_RESOURCE_GENERIC_REGISTER *status __unused,
			  const struct acpi_pstate *pstate)
{
	struct amd0f_fidvid fv;
	struct amd0f_xsit xsit;

	fv.fid = AMD0F_PST_CTL_FID(pstate->st_cval);
	fv.vid = AMD0F_PST_CTL_VID(pstate->st_cval);

	xsit.rvo = AMD0F_PST_CTL_RVO(pstate->st_cval);
	xsit.mvs = AMD0F_PST_CTL_MVS(pstate->st_cval);
	xsit.vst = AMD0F_PST_CTL_VST(pstate->st_cval);
	xsit.pll_time = AMD0F_PST_CTL_PLLTIME(pstate->st_cval);
	xsit.irt = AMD0F_PST_CTL_IRT(pstate->st_cval);

	return amd0f_set_fidvid(&fv, &xsit);
}

static const struct acpi_pstate *
acpi_pst_amd0f_get_pstate(const ACPI_RESOURCE_GENERIC_REGISTER * ctrl __unused,
			  const struct acpi_pstate *pstates, int npstates)
{
	struct amd0f_fidvid fv;
	int error, i;

	error = amd0f_get_fidvid(&fv);
	if (error)
		return NULL;

	for (i = 0; i < npstates; ++i) {
		const struct acpi_pstate *p = &pstates[i];

		if (fv.fid == AMD0F_PST_ST_FID(p->st_sval) &&
		    fv.vid == AMD0F_PST_ST_VID(p->st_sval))
			return p;
	}
	return NULL;
}
