/*-
 * Copyright (c) 1998 - 2008 Søren Schmidt <sos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* local prototypes */
static int ata_highpoint_chipinit(device_t dev);
static int ata_highpoint_allocate(device_t dev);
static void ata_highpoint_setmode(device_t dev, int mode);
static int ata_highpoint_check_80pin(device_t dev, int mode);

/*
 * HighPoint chipset support functions
 */
int
ata_highpoint_ident(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    struct ata_chip_id *idx;
    static struct ata_chip_id ids[] =
    {{ ATA_HPT374, 0x07, HPT374, 0x00,   ATA_UDMA6, "HPT374" },
     { ATA_HPT372, 0x02, HPT372, 0x00,   ATA_UDMA6, "HPT372N" },
     { ATA_HPT372, 0x01, HPT372, 0x00,   ATA_UDMA6, "HPT372" },
     { ATA_HPT371, 0x01, HPT372, 0x00,   ATA_UDMA6, "HPT371" },
     { ATA_HPT366, 0x05, HPT372, 0x00,   ATA_UDMA6, "HPT372" },
     { ATA_HPT366, 0x03, HPT370, 0x00,   ATA_UDMA5, "HPT370" },
     { ATA_HPT366, 0x02, HPT366, 0x00,   ATA_UDMA4, "HPT368" },
     { ATA_HPT366, 0x00, HPT366, HPTOLD, ATA_UDMA4, "HPT366" },
     { ATA_HPT302, 0x01, HPT372, 0x00,   ATA_UDMA6, "HPT302" },
     { 0, 0, 0, 0, 0, 0}};
    char buffer[64];

    if (!(idx = ata_match_chip(dev, ids)))
	return ENXIO;

    strcpy(buffer, "HighPoint ");
    strcat(buffer, idx->text);
    if (idx->cfg1 == HPT374) {
	if (pci_get_function(dev) == 0)
	    strcat(buffer, " (channel 0+1)");
	if (pci_get_function(dev) == 1)
	    strcat(buffer, " (channel 2+3)");
    }
    ksprintf(buffer, "%s %s controller", buffer, ata_mode2str(idx->max_dma));
    device_set_desc_copy(dev, buffer);
    ctlr->chip = idx;
    ctlr->chipinit = ata_highpoint_chipinit;
    return 0;
}

static int
ata_highpoint_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);

    if (ata_setup_interrupt(dev))
	return ENXIO;

    if (ctlr->chip->cfg2 == HPTOLD) {
	/* disable interrupt prediction */
	pci_write_config(dev, 0x51, (pci_read_config(dev, 0x51, 1) & ~0x80), 1);
    }
    else {
	/* disable interrupt prediction */
	pci_write_config(dev, 0x51, (pci_read_config(dev, 0x51, 1) & ~0x03), 1);
	pci_write_config(dev, 0x55, (pci_read_config(dev, 0x55, 1) & ~0x03), 1);

	/* enable interrupts */
	pci_write_config(dev, 0x5a, (pci_read_config(dev, 0x5a, 1) & ~0x10), 1);

	/* set clocks etc */
	if (ctlr->chip->cfg1 < HPT372)
	    pci_write_config(dev, 0x5b, 0x22, 1);
	else
	    pci_write_config(dev, 0x5b,
			     (pci_read_config(dev, 0x5b, 1) & 0x01) | 0x20, 1);
    }
    ctlr->allocate = ata_highpoint_allocate;
    ctlr->setmode = ata_highpoint_setmode;
    return 0;
}

static int
ata_highpoint_allocate(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    /* setup the usual register normal pci style */
    if (ata_pci_allocate(dev))
	return ENXIO;

    ch->flags |= ATA_ALWAYS_DMASTAT;
    return 0;
}

static void
ata_highpoint_setmode(device_t dev, int mode)
{
    device_t gparent = GRANDPARENT(dev);
    struct ata_pci_controller *ctlr = device_get_softc(gparent);
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    struct ata_device *atadev = device_get_softc(dev);
    int devno = (ch->unit << 1) + ATA_DEV(atadev->unit);
    int error;
    u_int32_t timings33[][4] = {
    /*    HPT366      HPT370      HPT372      HPT374               mode */
	{ 0x40d0a7aa, 0x06914e57, 0x0d029d5e, 0x0ac1f48a },     /* PIO 0 */
	{ 0x40d0a7a3, 0x06914e43, 0x0d029d26, 0x0ac1f465 },     /* PIO 1 */
	{ 0x40d0a753, 0x06514e33, 0x0c829ca6, 0x0a81f454 },     /* PIO 2 */
	{ 0x40c8a742, 0x06514e22, 0x0c829c84, 0x0a81f443 },     /* PIO 3 */
	{ 0x40c8a731, 0x06514e21, 0x0c829c62, 0x0a81f442 },     /* PIO 4 */
	{ 0x20c8a797, 0x26514e97, 0x2c82922e, 0x228082ea },     /* MWDMA 0 */
	{ 0x20c8a732, 0x26514e33, 0x2c829266, 0x22808254 },     /* MWDMA 1 */
	{ 0x20c8a731, 0x26514e21, 0x2c829262, 0x22808242 },     /* MWDMA 2 */
	{ 0x10c8a731, 0x16514e31, 0x1c829c62, 0x121882ea },     /* UDMA 0 */
	{ 0x10cba731, 0x164d4e31, 0x1c9a9c62, 0x12148254 },     /* UDMA 1 */
	{ 0x10caa731, 0x16494e31, 0x1c929c62, 0x120c8242 },     /* UDMA 2 */
	{ 0x10cfa731, 0x166d4e31, 0x1c8e9c62, 0x128c8242 },     /* UDMA 3 */
	{ 0x10c9a731, 0x16454e31, 0x1c8a9c62, 0x12ac8242 },     /* UDMA 4 */
	{ 0,          0x16454e31, 0x1c8a9c62, 0x12848242 },     /* UDMA 5 */
	{ 0,          0,          0x1c869c62, 0x12808242 }      /* UDMA 6 */
    };

    mode = ata_limit_mode(dev, mode, ctlr->chip->max_dma);

    if (ctlr->chip->cfg1 == HPT366 && ata_atapi(dev))
	mode = ata_limit_mode(dev, mode, ATA_PIO_MAX);

    mode = ata_highpoint_check_80pin(dev, mode);

    /*
     * most if not all HPT chips cant really handle that the device is
     * running at ATA_UDMA6/ATA133 speed, so we cheat at set the device to
     * a max of ATA_UDMA5/ATA100 to guard against suboptimal performance
     */
    error = ata_controlcmd(dev, ATA_SETFEATURES, ATA_SF_SETXFER, 0,
			   ata_limit_mode(dev, mode, ATA_UDMA5));
    if (bootverbose)
	device_printf(dev, "%ssetting %s on HighPoint chip\n",
		      (error) ? "FAILURE " : "", ata_mode2str(mode));
    if (!error)
	pci_write_config(gparent, 0x40 + (devno << 2),
			 timings33[ata_mode2idx(mode)][ctlr->chip->cfg1], 4);
    atadev->mode = mode;
}

static int
ata_highpoint_check_80pin(device_t dev, int mode)
{
    device_t gparent = GRANDPARENT(dev);
    struct ata_pci_controller *ctlr = device_get_softc(gparent);
    struct ata_channel *ch = device_get_softc(device_get_parent(dev));
    u_int8_t reg, val, res;

    if (ctlr->chip->cfg1 == HPT374 && pci_get_function(gparent) == 1) {
	reg = ch->unit ? 0x57 : 0x53;
	val = pci_read_config(gparent, reg, 1);
	pci_write_config(gparent, reg, val | 0x80, 1);
    }
    else {
	reg = 0x5b;
	val = pci_read_config(gparent, reg, 1);
	pci_write_config(gparent, reg, val & 0xfe, 1);
    }
    res = pci_read_config(gparent, 0x5a, 1) & (ch->unit ? 0x1:0x2);
    pci_write_config(gparent, reg, val, 1);

    if (mode > ATA_UDMA2 && res) {
	ata_print_cable(dev, "controller");
	mode = ATA_UDMA2;
    }
    return mode;
}
