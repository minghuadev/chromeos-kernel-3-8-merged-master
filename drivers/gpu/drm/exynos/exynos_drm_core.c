/* exynos_drm_core.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Author:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include "exynos_drm_drv.h"
#include "exynos_drm_crtc.h"
#include "exynos_drm_encoder.h"
#include "exynos_drm_connector.h"
#include "exynos_drm_fbdev.h"

static LIST_HEAD(exynos_drm_subdrv_list);
static LIST_HEAD(exynos_drm_manager_list);
static LIST_HEAD(exynos_drm_display_list);

static int exynos_drm_create_enc_conn(struct drm_device *dev,
					struct exynos_drm_display *display)
{
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct exynos_drm_manager *manager;
	int ret;
	unsigned long possible_crtcs = 0;

	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	/* Find possible crtcs for this display */
	list_for_each_entry(manager, &exynos_drm_manager_list, list)
		if (manager->type == display->type)
			possible_crtcs |= 1 << manager->pipe;

	/* create and initialize a encoder for this sub driver. */
	encoder = exynos_drm_encoder_create(dev, display, possible_crtcs);
	if (!encoder) {
		DRM_ERROR("failed to create encoder\n");
		return -EFAULT;
	}

	/*
	 * create and initialize a connector for this sub driver and
	 * attach the encoder created above to the connector.
	 */
	connector = exynos_drm_connector_create(dev, encoder);
	if (!connector) {
		DRM_ERROR("failed to create connector\n");
		ret = -EFAULT;
		goto err_destroy_encoder;
	}

	display->encoder = encoder;
	display->connector = connector;

	return 0;

err_destroy_encoder:
	encoder->funcs->destroy(encoder);
	return ret;
}

static int exynos_drm_subdrv_probe(struct drm_device *dev,
					struct exynos_drm_subdrv *subdrv)
{
	if (subdrv->probe) {
		int ret;

		subdrv->drm_dev = dev;

		/*
		 * this probe callback would be called by sub driver
		 * after setting of all resources to this sub driver,
		 * such as clock, irq and register map are done or by load()
		 * of exynos drm driver.
		 *
		 * P.S. note that this driver is considered for modularization.
		 */
		ret = subdrv->probe(dev, subdrv->dev);
		if (ret)
			return ret;
	}

	return 0;
}

static void exynos_drm_subdrv_remove(struct drm_device *dev,
				      struct exynos_drm_subdrv *subdrv)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	if (subdrv->remove)
		subdrv->remove(dev, subdrv->dev);
}

int exynos_drm_initialize_managers(struct drm_device *dev)
{
	struct exynos_drm_manager *manager, *n;
	int ret, pipe = 0;

	list_for_each_entry(manager, &exynos_drm_manager_list, list) {
		if (manager->ops->initialize) {
			ret = manager->ops->initialize(manager->ctx, dev, pipe);
			if (ret) {
				DRM_ERROR("Mgr init [%d] failed with %d\n",
						manager->type, ret);
				goto err;
			}
		}

		manager->drm_dev = dev;
		manager->pipe = pipe++;

		ret = exynos_drm_crtc_create(manager);
		if (ret) {
			DRM_ERROR("CRTC create [%d] failed with %d\n",
					manager->type, ret);
			goto err;
		}
	}
	return 0;

err:
	list_for_each_entry_safe(manager, n, &exynos_drm_manager_list, list) {
		if (pipe-- > 0)
			exynos_drm_manager_unregister(manager);
		else
			list_del(&manager->list);
	}
	return ret;
}

void exynos_drm_remove_managers(struct drm_device *dev)
{
	struct exynos_drm_manager *manager, *n;

	list_for_each_entry_safe(manager, n, &exynos_drm_manager_list, list)
		exynos_drm_manager_unregister(manager);
}

int exynos_drm_initialize_displays(struct drm_device *dev)
{
	struct exynos_drm_display *display, *n;
	int ret, initialized = 0;

	list_for_each_entry(display, &exynos_drm_display_list, list) {
		if (display->ops->initialize) {
			ret = display->ops->initialize(display->ctx, dev);
			if (ret) {
				DRM_ERROR("Display init [%d] failed with %d\n",
						display->type, ret);
				goto err;
			}
		}

		initialized++;

		ret = exynos_drm_create_enc_conn(dev, display);
		if (ret) {
			DRM_ERROR("Encoder create [%d] failed with %d\n",
					display->type, ret);
			goto err;
		}
	}
	return 0;

err:
	list_for_each_entry_safe(display, n, &exynos_drm_display_list, list) {
		if (initialized-- > 0)
			exynos_drm_display_unregister(display);
		else
			list_del(&display->list);
	}
	return ret;
}

void exynos_drm_remove_displays(struct drm_device *dev)
{
	struct exynos_drm_display *display, *n;

	list_for_each_entry_safe(display, n, &exynos_drm_display_list, list)
		exynos_drm_display_unregister(display);
}

int exynos_drm_device_register(struct drm_device *dev)
{
	struct exynos_drm_subdrv *subdrv, *n;
	int err;

	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	if (!dev)
		return -EINVAL;

	list_for_each_entry_safe(subdrv, n, &exynos_drm_subdrv_list, list) {
		err = exynos_drm_subdrv_probe(dev, subdrv);
		if (err) {
			DRM_DEBUG("exynos drm subdrv probe failed.\n");
			list_del(&subdrv->list);
			continue;
		}
	}

	return 0;
}

int exynos_drm_device_unregister(struct drm_device *dev)
{
	struct exynos_drm_subdrv *subdrv;

	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	if (!dev) {
		WARN(1, "Unexpected drm device unregister!\n");
		return -EINVAL;
	}

	list_for_each_entry(subdrv, &exynos_drm_subdrv_list, list) {
		exynos_drm_subdrv_remove(dev, subdrv);
	}

	return 0;
}

int exynos_drm_manager_register(struct exynos_drm_manager *manager)
{
	BUG_ON(!manager->ops);
	list_add_tail(&manager->list, &exynos_drm_manager_list);
	return 0;
}

int exynos_drm_manager_unregister(struct exynos_drm_manager *manager)
{
	if (manager->ops->remove)
		manager->ops->remove(manager->ctx);

	list_del(&manager->list);
	return 0;
}

int exynos_drm_display_register(struct exynos_drm_display *display)
{
	BUG_ON(!display->ops);
	list_add_tail(&display->list, &exynos_drm_display_list);
	return 0;
}

int exynos_drm_display_unregister(struct exynos_drm_display *display)
{
	if (display->ops->remove)
		display->ops->remove(display->ctx);

	list_del(&display->list);
	return 0;
}

int exynos_drm_subdrv_register(struct exynos_drm_subdrv *subdrv)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	if (!subdrv)
		return -EINVAL;

	list_add_tail(&subdrv->list, &exynos_drm_subdrv_list);

	return 0;
}

int exynos_drm_subdrv_unregister(struct exynos_drm_subdrv *subdrv)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	if (!subdrv)
		return -EINVAL;

	list_del(&subdrv->list);

	return 0;
}

int exynos_drm_subdrv_open(struct drm_device *dev, struct drm_file *file)
{
	struct exynos_drm_subdrv *subdrv;
	int ret;

	list_for_each_entry(subdrv, &exynos_drm_subdrv_list, list) {
		if (subdrv->open) {
			ret = subdrv->open(dev, subdrv->dev, file);
			if (ret)
				goto err;
		}
	}

	return 0;

err:
	list_for_each_entry_reverse(subdrv, &subdrv->list, list) {
		if (subdrv->close)
			subdrv->close(dev, subdrv->dev, file);
	}
	return ret;
}

void exynos_drm_subdrv_close(struct drm_device *dev, struct drm_file *file)
{
	struct exynos_drm_subdrv *subdrv;

	list_for_each_entry(subdrv, &exynos_drm_subdrv_list, list) {
		if (subdrv->close)
			subdrv->close(dev, subdrv->dev, file);
	}
}
