/*
 * Copyright (c) 2015 Imre Vadász <imre@vdsz.com>
 * Copyright (c) 2015 Rimvydas Jasinskas
 *
 * DRM Dragonfly-specific helper functions
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <sys/libkern.h>
#include <sys/ctype.h>
#include <drm/drmP.h>

/*
 * An implementation of fb_get_options()
 * This can be used to set the video mode used for the syscons fb console,
 * a la "video=..." in linux.
 */
int
fb_get_options(const char *connector_name, char **option)
{
	char buf[128], str[1024];
	int i;

	/*
	 * This hack allows us to use drm.video.lvds1="<video-mode>"
	 * in loader.conf, where linux would use video=LVDS-1:<video-mode>.
	 * e.g. drm.video.lvds1=1024x768 sets the LVDS-1 connector to
	 * a 1024x768 video mode in the syscons framebuffer console.
	 * See https://wiki.archlinux.org/index.php/Kernel_mode_setting
	 * for an explanation of the video mode command line option.
	 * (This corresponds to the video= Linux kernel command-line
	 * option)
	 */
	memset(str, 0, sizeof(str));
	ksnprintf(buf, sizeof(buf), "drm.video.%s", connector_name);
	i = 0;
	while (i < strlen(buf)) {
		buf[i] = tolower(buf[i]);
		if (buf[i] == '-') {
			memmove(&buf[i], &buf[i+1], strlen(buf)-i);
		} else {
			i++;
		}
	}
	kprintf("looking up kenv for \"%s\"\n", buf);
	if (kgetenv_string(buf, str, sizeof(str)-1)) {
		kprintf("found kenv %s=%s\n", buf, str);
		*option = kstrdup(str, M_DRM);
		return (0);
	} else {
		kprintf("didn't find value for kenv %s\n", buf);
		return (1);
	}
}

/*
 * Implement simplified version of kvasnrprintf() for drm needs using
 * M_DRM and kvsnprintf(). Since it is unclear what string size is
 * optimal thus use of an actual length.
 */
char *drm_vasprintf(int flags, const char *format, __va_list ap)
{
	char *str;
	size_t size;
	__va_list aq;

	__va_copy(aq, ap);
	size = kvsnprintf(NULL, 0, format, aq);
	__va_end(aq);

	str = kmalloc(size+1, M_DRM, flags);
	if (str == NULL)
		return NULL;

	kvsnprintf(str, size+1, format, ap);

	return str;
}

/* mimic ksnrprintf(), return pointer to char* and match drm api */
char *drm_asprintf(int flags, const char *format, ...)
{
	char *str;
	__va_list ap;

	__va_start(ap, format);
	str = drm_vasprintf(flags, format, ap);
	__va_end(ap);

	return str;
}

/*
 * XXX pci glue logic helpers
 * Should be done in drm_pci_init(), pending drm update.
 * Assumes static runtime data.
 * Only for usage in *_driver_[un]load()
 */

static void drm_fill_pdev(struct device *dev, struct pci_dev *pdev)
{
	pdev->dev = dev;
	pdev->vendor = pci_get_vendor(dev);
	pdev->device = pci_get_device(dev);
	pdev->subsystem_vendor = pci_get_subvendor(dev);
	pdev->subsystem_device = pci_get_subdevice(dev);
}

void drm_init_pdev(struct device *dev, struct pci_dev **pdev)
{
	BUG_ON(*pdev != NULL);

	*pdev = kzalloc(sizeof(struct pci_dev), GFP_KERNEL);
	drm_fill_pdev(dev, *pdev);

	(*pdev)->bus = kzalloc(sizeof(struct pci_bus), GFP_KERNEL);
	(*pdev)->bus->self = kzalloc(sizeof(struct pci_dev), GFP_KERNEL);

	drm_fill_pdev(device_get_parent(dev), (*pdev)->bus->self);
	(*pdev)->bus->number = pci_get_bus(dev);
}

void drm_fini_pdev(struct pci_dev **pdev)
{
	kfree((*pdev)->bus->self);
	kfree((*pdev)->bus);

	kfree(*pdev);
}
