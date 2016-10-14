/*
 * (c) Copyright 2016 Hewlett Packard Enterprise Development LP
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */
#ifndef __VXLAN_PLUGIN_H__
#define __VXLAN_PLUGIN_H__

#include "reconfigure-blocks.h"
#include "vxlan-asic-plugin.h"
#define VXLAN_PLUGIN_NAME  "vxlan"
#define VXLAN_PLUGIN_MAJOR  0
#define VXLAN_PLUGIN_MINOR  1
#define VXLAN_PLUGIN_PRIORITY  NO_PRIORITY

extern struct vxlan_asic_plugin_interface *get_asic_plugin(void);
extern void vxlan_bridge_init_cb(struct blk_params *blk_params);
extern void vxlan_bridge_reconfigure_cb(struct blk_params *blk_params);
extern void vxlan_port_add_cb(struct blk_params *blk_params);
extern void vxlan_port_update_cb(struct blk_params *blk_params);
extern void vxlan_port_delete_cb(struct blk_params *blk_params);

#endif
