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
 *
 * File: vxlan-plugin.c
 *
 * Purpose: This file contains switchd changes for the global VXLAN to VNI mapping
 * CLI command.
 */

#include <config.h>
#include <errno.h>
#include <assert.h>

#include "openvswitch/vlog.h"
#include "plugin-extensions.h"
#include "vxlan_plugin.h"
#include "logical-switch.h"
#include "asic-plugin.h"


VLOG_DEFINE_THIS_MODULE(vxlan_plugin);
static struct asic_plugin_interface *asic_plugin = NULL;
static struct asic_plugin_interface*
get_asic_plugin(void);

struct vlan_node {
    char *name;
    int vlan_id;
    uint64_t tunnel_key;
};

struct port_node {
    char *name;
    int vlan_id;
    char *vlan_name;
    int vlan_mode;
};

struct vxlan {
    struct shash vlans;
    struct shash ports;

};

static struct vxlan *vxlan;

static void vxlan_create(void)
{
    vxlan = xzalloc(sizeof(*vxlan));
    if (!vxlan) {
        VLOG_ERR("Failed to allocated memory for vxlan plugin");
        assert(0);
    }
    shash_init(&vxlan->vlans);
    shash_init(&vxlan->ports);
    VLOG_INFO("Successfully created vxlan state");
}

int init(int phase_id)
{
    int ret = 0;
    struct plugin_extension_interface *extension = NULL;

    VLOG_INFO("VXLAN plugin initialization");

    ret = find_plugin_extension(ASIC_PLUGIN_INTERFACE_NAME,
                                ASIC_PLUGIN_INTERFACE_MAJOR,
                                ASIC_PLUGIN_INTERFACE_MINOR,
                                &extension);
    if (ret == 0) {
        VLOG_INFO("Found [%s] asic plugin extension...", ASIC_PLUGIN_INTERFACE_NAME);
        asic_plugin = (struct asic_plugin_interface *)extension->plugin_interface;
    }
    else {
        VLOG_WARN("%s (v%d.%d) not found", ASIC_PLUGIN_INTERFACE_NAME,
                  ASIC_PLUGIN_INTERFACE_MAJOR,
                  ASIC_PLUGIN_INTERFACE_MINOR);
    }
    VLOG_INFO("[%s] Registering BLK_BRIDGE_INIT", VXLAN_PLUGIN_NAME);
    register_reconfigure_callback(&vxlan_bridge_init_cb,
                                  BLK_BRIDGE_INIT,
                                  VXLAN_PLUGIN_PRIORITY);

    VLOG_INFO("[%s] Registering BLK_BR_FEATURE_RECONFIG", VXLAN_PLUGIN_NAME);
    register_reconfigure_callback(&vxlan_bridge_reconfigure_cb,
                                  BLK_BR_FEATURE_RECONFIG,
                                  VXLAN_PLUGIN_PRIORITY);

    VLOG_INFO("[%s] Registering BLK_BR_PORT_ADD", VXLAN_PLUGIN_NAME);
    register_reconfigure_callback(&vxlan_port_add_cb,
                                  BLK_BR_ADD_PORTS,
                                  VXLAN_PLUGIN_PRIORITY);

    VLOG_INFO("[%s] Registering BLK_BR_PORT_UPDATE", VXLAN_PLUGIN_NAME);
    register_reconfigure_callback(&vxlan_port_update_cb,
                                  BLK_BR_PORT_UPDATE,
                                  VXLAN_PLUGIN_PRIORITY);

    VLOG_INFO("[%s] Registering BLK_BR_DELETE_PORTS", VXLAN_PLUGIN_NAME);
    register_reconfigure_callback(&vxlan_port_delete_cb,
                                  BLK_BR_DELETE_PORTS,
                                  VXLAN_PLUGIN_PRIORITY);

    VLOG_INFO("[%s] Registering BLK_BRIDGE_INIT", VXLAN_PLUGIN_NAME);
    register_reconfigure_callback(&log_switch_callback_bridge_init,
                                  BLK_BRIDGE_INIT, VXLAN_PLUGIN_PRIORITY);

    VLOG_INFO("[%s] Registering BLK_BR_FEATURE_RECONFIG", VXLAN_PLUGIN_NAME);
    register_reconfigure_callback(&log_switch_callback_bridge_reconfig,
                                  BLK_BR_FEATURE_RECONFIG, VXLAN_PLUGIN_PRIORITY);
    vxlan_create();

    return ret;
}


int run(void)
{
    return 0;
}

int wait(void)
{
    return 0;
}

int destroy(void)
{
    return 0;
}

void vxlan_bridge_init_cb(struct blk_params *blk_params)
{
    ovsdb_idl_omit_alert(blk_params->idl, &ovsrec_vlan_col_tunnel_key);
}


/**
 * bridge_reconfigure BLK_BRIDGE_INIT callback handler
 */
void log_switch_callback_bridge_init(struct blk_params *blk_params)
{
    /* Enable writes into various Logical Switch columns. */
    ovsdb_idl_omit_alert(blk_params->idl, &ovsrec_logical_switch_col_tunnel_key);
    ovsdb_idl_omit_alert(blk_params->idl, &ovsrec_logical_switch_col_bridge);
    ovsdb_idl_omit_alert(blk_params->idl, &ovsrec_logical_switch_col_from);
    ovsdb_idl_omit_alert(blk_params->idl, &ovsrec_logical_switch_col_description);
    ovsdb_idl_omit_alert(blk_params->idl, &ovsrec_logical_switch_col_name);
}

/**
 * add Logical Switch
 */
static void
logical_switch_create(struct bridge *br,
        const struct ovsrec_logical_switch *logical_switch_cfg)
{
    struct logical_switch *new_logical_switch = NULL;
    struct logical_switch_node ofp_log_switch;
    char hash_str[LSWITCH_HASH_STR_SIZE];

    if(!br || !logical_switch_cfg) {
        return;
    }

    /* Allocate structure to save state information for this logical_switch. */
    new_logical_switch = xzalloc(sizeof(struct logical_switch));

    /* The hash should really be bridge.name+type+tunnel_key */
    logical_switch_hash(hash_str, sizeof(hash_str), br->name,
                        logical_switch_cfg->tunnel_key);
    /* No need to check for uniqueness because
     * that's done before we call this function */
    hmap_insert(&br->logical_switches, &new_logical_switch->node,
                hash_string(hash_str, 0));

    new_logical_switch->br = br;
    new_logical_switch->cfg = logical_switch_cfg;
    new_logical_switch->tunnel_key = (int)logical_switch_cfg->tunnel_key;
    new_logical_switch->name = xstrdup(logical_switch_cfg->name);
    new_logical_switch->description = xstrdup(logical_switch_cfg->description);

    ofp_log_switch.name = logical_switch_cfg->name;
    ofp_log_switch.tunnel_key = (int)logical_switch_cfg->tunnel_key;
    ofp_log_switch.name = logical_switch_cfg->name;
    ofp_log_switch.description = logical_switch_cfg->description;

    ofproto_set_logical_switch(br->ofproto, NULL, LSWITCH_ACTION_ADD,
            &ofp_log_switch);
}

/**
 * delete Logical Switch
 */
static void
logical_switch_delete(struct logical_switch *logical_switch)
{
    struct logical_switch_node ofp_log_switch;
    struct bridge *br;

    if (!logical_switch) {
        return;
    }

    ofp_log_switch.name = logical_switch->name;
    ofp_log_switch.description = logical_switch->description;
    ofp_log_switch.tunnel_key = logical_switch->tunnel_key;

    br = logical_switch->br;

    ofproto_set_logical_switch(br->ofproto, NULL, LSWITCH_ACTION_DEL,
            &ofp_log_switch);

    hmap_remove(&br->logical_switches, &logical_switch->node);
    free(logical_switch->name);
    free(logical_switch->description);
    free(logical_switch);
}

/**
 * update Logical Switch
 */
static void
logical_switch_update(struct logical_switch *cur_logical_switch,
        const struct ovsrec_logical_switch *logical_switch)
{
    struct logical_switch_node ofp_log_switch;
    struct bridge *br;

    if (!cur_logical_switch || !logical_switch) {
        return;
    }

    br = cur_logical_switch->br;

    /* The tunnel key should not change. If it does, it will be seen
     * as a new logical switch */
    if((0 != strcmp(cur_logical_switch->description, logical_switch->description)) ||
       (0 != strcmp(cur_logical_switch->name, logical_switch->name)))
    {
        VLOG_DBG("Found a modified logical switch: "
                 "name=%s key=%ld description=%s",
                 logical_switch->name, logical_switch->tunnel_key,
                 logical_switch->description);

        cur_logical_switch->description = xstrdup(logical_switch->description);
        cur_logical_switch->name = xstrdup(logical_switch->name);

        ofp_log_switch.name = logical_switch->name;
        ofp_log_switch.description = logical_switch->description;
        ofp_log_switch.tunnel_key = logical_switch->tunnel_key;
        ofproto_set_logical_switch(br->ofproto, NULL, LSWITCH_ACTION_MOD,
            &ofp_log_switch);
    }

}

/**
 * bridge_reconfigure BLK_BR_FEATURE_RECONFIG callback
 *
 * called after everything for a bridge has been add/deleted/updated
 */
void
log_switch_callback_bridge_reconfig(struct blk_params *blk_params)
{
    struct logical_switch *logical_switch, *next, *found;
    struct shash current_idl_logical_switches;
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    char hash_str[LSWITCH_HASH_STR_SIZE];

    if(!blk_params->br) {
        return;
    }

    logical_switch_row = ovsrec_logical_switch_first(blk_params->idl);
    if (!logical_switch_row) {
        VLOG_DBG("No rows in Logical Switch table, delete all in local hash");

        /* Maybe all the Logical Switches got deleted */
        HMAP_FOR_EACH_SAFE(logical_switch, next, node, &blk_params->br->logical_switches) {
            logical_switch_delete(logical_switch);
        }
        return;
    }

    if ((!OVSREC_IDL_ANY_TABLE_ROWS_MODIFIED(logical_switch_row, blk_params->idl_seqno)) &&
        (!OVSREC_IDL_ANY_TABLE_ROWS_DELETED(logical_switch_row, blk_params->idl_seqno)) &&
        (!OVSREC_IDL_ANY_TABLE_ROWS_INSERTED(logical_switch_row, blk_params->idl_seqno))) {
        VLOG_DBG("No modification in Logical Switch table");
        return;
    }

    /* Collect all the logical_switches present on this Bridge */
    shash_init(&current_idl_logical_switches);
    OVSREC_LOGICAL_SWITCH_FOR_EACH(logical_switch_row, blk_params->idl) {
        if (strcmp(blk_params->br->cfg->name, logical_switch_row->bridge->name) == 0) {
            logical_switch_hash(hash_str, sizeof(hash_str), blk_params->br->name,
                                logical_switch_row->tunnel_key);
            if (!shash_add_once(&current_idl_logical_switches,
                    hash_str, logical_switch_row)) {
                VLOG_WARN("logical switch %s (key %ld) specified twice",
                          logical_switch_row->name, logical_switch_row->tunnel_key);
            }
        }
    }

    /* Delete old logical_switches. */
    logical_switch_row = ovsrec_logical_switch_first(blk_params->idl);
    if (OVSREC_IDL_ANY_TABLE_ROWS_DELETED(logical_switch_row, blk_params->idl_seqno) ||
        OVSREC_IDL_ANY_TABLE_ROWS_MODIFIED(logical_switch_row, blk_params->idl_seqno)) {
        HMAP_FOR_EACH_SAFE (logical_switch, next, node, &blk_params->br->logical_switches) {
            found = logical_switch_lookup_by_key(
                &current_idl_logical_switches,
                blk_params->br->name, logical_switch->tunnel_key);
            if (!found) {
                VLOG_DBG("Found a deleted logical_switch %s (key %d)",
                         logical_switch->name, logical_switch->tunnel_key);
                /* Need to update ofproto now since this logical_switch
                 * won't be around for the "check for changes"
                 * loop below. */
                logical_switch_delete(logical_switch);
            }
        }
    }

    /* Add new logical_switches. */
    logical_switch_row = ovsrec_logical_switch_first(blk_params->idl);
    if (OVSREC_IDL_ANY_TABLE_ROWS_INSERTED(logical_switch_row, blk_params->idl_seqno) ||
        OVSREC_IDL_ANY_TABLE_ROWS_MODIFIED(logical_switch_row, blk_params->idl_seqno)) {
        OVSREC_LOGICAL_SWITCH_FOR_EACH(logical_switch_row, blk_params->idl) {
            found = logical_switch_lookup_by_key(&current_idl_logical_switches,
                                                 blk_params->br->name,
                                                 logical_switch_row->tunnel_key);
            if (!found) {
                VLOG_DBG("Found an added logical_switch %s %ld",
                    logical_switch_row->name, logical_switch_row->tunnel_key);
                logical_switch_create(blk_params->br, logical_switch_row);
            }
        }
    }

    /* Check for changes in the logical_switch row entries. */
    logical_switch_row = ovsrec_logical_switch_first(blk_params->idl);
    if (OVSREC_IDL_ANY_TABLE_ROWS_MODIFIED(logical_switch_row, blk_params->idl_seqno)) {
        HMAP_FOR_EACH (logical_switch, node, &blk_params->br->logical_switches) {
            const struct ovsrec_logical_switch *row = logical_switch->cfg;

            /* Check for changes to row. */
            if (!OVSREC_IDL_IS_ROW_INSERTED(row, blk_params->idl_seqno) &&
                 OVSREC_IDL_IS_ROW_MODIFIED(row, blk_params->idl_seqno)) {
                    logical_switch_update(logical_switch, row);
            }
       }
    }

    /* Destroy the shash of the IDL logical_switches */
    shash_destroy(&current_idl_logical_switches);
}

/* sets (add/delete/update) Logical Switch parameters in an ofproto. */
int
ofproto_set_logical_switch(const struct ofproto *ofproto, void *aux,
                           enum logical_switch_action action,
                           struct logical_switch_node *log_switch)
{
    int rc;
    struct asic_plugin_interface *plugin = get_asic_plugin();

    if (plugin == NULL) {
        return EOPNOTSUPP;
    }

    rc = plugin->set_logical_switch ?
         plugin->set_logical_switch(ofproto, aux, action, log_switch) :
         EOPNOTSUPP;

    VLOG_DBG("%s rc (%d) op(%d) name (%s) key (%d)",
             __func__, rc, action, log_switch->name, log_switch->tunnel_key);
    return rc;
}

struct logical_switch *
logical_switch_lookup_by_key(const struct shash *hmap, const char *br_name, const int key)
{
    struct logical_switch *logical_switch = NULL;
    char hash_str[LSWITCH_HASH_STR_SIZE];

    if((NULL != hmap) && (NULL != br_name)) {
        logical_switch_hash(hash_str, sizeof(hash_str), br_name, key);
        logical_switch = shash_find_data(hmap, hash_str);
    }

    return logical_switch;
}


static struct asic_plugin_interface*
get_asic_plugin(void)
{
    int ret = 0;
    struct plugin_extension_interface *extension = NULL;

    if (asic_plugin)
        return asic_plugin;

    ret = find_plugin_extension(ASIC_PLUGIN_INTERFACE_NAME,
                                ASIC_PLUGIN_INTERFACE_MAJOR,
                                ASIC_PLUGIN_INTERFACE_MINOR,
                                &extension);
    if (ret == 0) {
        VLOG_INFO("Found [%s] asic plugin extension...", ASIC_PLUGIN_INTERFACE_NAME);
        asic_plugin = (struct asic_plugin_interface *)extension->plugin_interface;
    }
    else {
        VLOG_WARN("%s (v%d.%d) not found", ASIC_PLUGIN_INTERFACE_NAME,
                  ASIC_PLUGIN_INTERFACE_MAJOR,
                  ASIC_PLUGIN_INTERFACE_MINOR);
        assert(0);
    }
    return asic_plugin;
}


static int vxlan_check_vlan_state_change(struct blk_params *blk_params)
{
    struct bridge *br = blk_params->br;
    unsigned int idl_seqno = blk_params->idl_seqno;
    int i;
    struct asic_plugin_interface *plugin = get_asic_plugin();

    for (i = 0; i < br->cfg->n_vlans; i++) {
        const struct ovsrec_vlan *vlan_row = br->cfg->vlans[i];
        const char *name = vlan_row->name;

        if (OVSREC_IDL_IS_ROW_INSERTED(vlan_row, idl_seqno)) {
            VLOG_DBG("VLAN row inserted for VLAN %ld, name %s, tunnel %p",
                     vlan_row->id, vlan_row->name, vlan_row->tunnel_key);
            // Update global state
            struct vlan_node *node;
            node = xzalloc(sizeof(struct vlan_node));
            node->name = xstrdup(name);
            node->tunnel_key = 0;
            node->vlan_id = vlan_row->id;
            if (!shash_add_once(&vxlan->vlans, vlan_row->name, node)) {
                VLOG_WARN("VLAN %s specified twice", vlan_row->name);
            }
        }
        /*
         * When VNI is configured for a VLAN somehow we don't receive
         * row modified event from bridge.c, hence need to check
         * against our global state for any modifications everytime
         * this function is called.
         */
        struct vlan_node *node;
        node = shash_find_data(&vxlan->vlans, name);
        if (node) {
            VLOG_DBG("Found vlan in global hash for %s, tunnel %ld, node name %s",
                      name, node->tunnel_key, node->name);
            // Compare if data has changed
            const struct ovsrec_logical_switch *tunnel_key_row = vlan_row->tunnel_key;
            uint64_t old_key = node->tunnel_key;
            uint64_t new_key = tunnel_key_row ? tunnel_key_row->tunnel_key : 0;
            if (old_key != new_key) {
                if (old_key) {
                    // Unbind all ports on this VLAN
                    VLOG_INFO("Unbinding all ports on VLAN %d, old tunnel key %ld",
                              node->vlan_id, old_key);
                    if (plugin->vport_unbind_all_ports_on_vlan) {
                        plugin->vport_unbind_all_ports_on_vlan(old_key,
                                                               node->vlan_id);
                    } else {
                        VLOG_ERR("No PD function vport_unbind_all_ports_on_vlan found");
                        assert(0);
                    }
                }

                if (new_key) {
                    VLOG_DBG("VLAN bound to new tunnel key %ld",
                             new_key);
                    if (plugin->vport_bind_all_ports_on_vlan) {
                        VLOG_INFO("Binding all ports on VLAN %d to new tunnel key %ld",
                                  node->vlan_id, new_key);
                        plugin->vport_bind_all_ports_on_vlan(new_key,
                                                             node->vlan_id);
                    } else {
                        VLOG_ERR("No PD function vport_bind_all_ports_on_vlan found");
                        assert(0);
                    }
                }
                // Update tunnel key
                node->tunnel_key = new_key;
            }
        } // if 'node'
    }
    return 0;
}


void vxlan_bridge_reconfigure_cb(struct blk_params *blk_params)
{
    if (!blk_params->br) {
        VLOG_ERR("Bridge not configured");
        return;
    }

    // Check for vlan table changes
    vxlan_check_vlan_state_change(blk_params);
}


void vxlan_port_add_cb(struct blk_params *blk_params)
{
    struct bridge *br = blk_params->br;
    unsigned int idl_seqno = blk_params->idl_seqno;
    int i;

    for (i = 0; i < br->cfg->n_ports; i++) {
        const struct ovsrec_port *port_row = br->cfg->ports[i];
        const char *name = port_row->name;
        if (OVSREC_IDL_IS_ROW_INSERTED(port_row, idl_seqno) ||
            OVSREC_IDL_IS_ROW_MODIFIED(port_row, idl_seqno)) {
            VLOG_DBG("Port row inserted %s", port_row->name);
            // update global state
            struct port_node *node;
            node = xzalloc(sizeof(*node));
            node->name = xstrdup(name);
            node->vlan_id = 0;
            if (!shash_add_once(&vxlan->ports, name, node)) {
                VLOG_WARN("Port %s specified twice", name);
            }
        }
    }
}


static void vxlan_bind_port_vlan(const char *vlan_name, struct port *port)
{
    uint64_t tunnel_key;
    struct vlan_node *vlan_node;

    vlan_node = shash_find_data(&vxlan->vlans, vlan_name);
    if (!vlan_node) {
        VLOG_ERR("%s not configured", vlan_name);
        return;
    }
    tunnel_key = vlan_node->tunnel_key;
    VLOG_DBG("Found %s bound to tunnel key %ld",
             vlan_name, tunnel_key);
    if (!tunnel_key) {
        VLOG_INFO("%s not bound to tunnel key %ld", vlan_name, tunnel_key);
        return;
    }
    struct asic_plugin_interface *plugin;
    plugin = get_asic_plugin();
    if (plugin->vport_bind_port_on_vlan) {
        plugin->vport_bind_port_on_vlan(tunnel_key,
                                        vlan_node->vlan_id,
                                        port);
    } else {
        VLOG_ERR("No PD function ops_vport_bind_port_on_vlan found");
        assert(0);
    }
}



static void vxlan_unbind_port_vlan(const char *vlan_name, struct port *port)
{
    uint64_t tunnel_key;
    struct vlan_node *vlan_node;

    vlan_node = shash_find_data(&vxlan->vlans, vlan_name);
    if (!vlan_node) {
        VLOG_ERR("VLAN %s not configured", vlan_name);
        return;
    }
    tunnel_key = vlan_node->tunnel_key;
    VLOG_DBG("Found %s bound to tunnel key %ld",
             vlan_name, tunnel_key);
    if (!tunnel_key) {
        VLOG_INFO("%s not bound to tunnel key %ld", vlan_name, tunnel_key);
        return;
    }
    struct asic_plugin_interface *plugin;
    plugin = get_asic_plugin();
    if (plugin->vport_unbind_port_on_vlan) {
        plugin->vport_unbind_port_on_vlan(tunnel_key,
                                          vlan_node->vlan_id,
                                          port);
    } else {
        VLOG_ERR("No PD function ops_vport_unbind_port_on_vlan found");
        assert(0);
    }
}


void vxlan_port_update_cb(struct blk_params *blk_params)
{
    struct port *port;

    if (!blk_params->br) {
        VLOG_ERR("Bridge not configured");
        return;
    }
    if (!blk_params->port) {
        VLOG_ERR("Port not configured");
        return;
    }

    port = blk_params->port;
    const struct ovsrec_port *port_row = port->cfg;
    char *name = port_row->name;

    if ((port_row->vlan_mode) && (strcmp(port_row->vlan_mode, "access"))) {
        VLOG_INFO("Only access ports are handled for now");
        return;
    }

    // Check for port update
    struct port_node *node;
    node = shash_find_data(&vxlan->ports, name);
    if (node) {
        VLOG_INFO("Found port %s in global hash", name);

        // Check for VLAN update
        uint64_t old_vlan = node->vlan_id;
        uint64_t new_vlan = port_row->vlan_tag ? port_row->vlan_tag->id : 0;
        if (old_vlan != new_vlan) {
            if (old_vlan) {
                VLOG_INFO("Unbind port %s on old VLAN %ld", name, old_vlan);
                vxlan_unbind_port_vlan(node->vlan_name, port);
            }

            if (new_vlan) {
                VLOG_INFO("Bind port %s on new VLAN %ld", name, new_vlan);
                vxlan_bind_port_vlan(port_row->vlan_tag->name, port);
            }
            // Update VLAN
            node->vlan_id = new_vlan;
            node->vlan_name = xstrdup(port_row->vlan_tag->name);
        }

    }
}


void vxlan_port_delete_cb(struct blk_params *blk_params)
{
    struct bridge *br = blk_params->br;
    struct port *port, *next;
    struct port_node *node;
    const struct ovsrec_port *cfg;

    if (!blk_params->br) {
        VLOG_ERR("Bridge not configured");
        return;
    }

    HMAP_FOR_EACH_SAFE (port, next, hmap_node, &br->ports) {
        cfg = shash_find_data(&br->wanted_ports, port->name);
        if (!cfg) {
            VLOG_DBG("Found deleted port %s", port->name);
            node = shash_find_data(&vxlan->ports, port->name);
            if (!node) {
                VLOG_ERR("Received delete port event for already deleted port %s",
                         port->name);
                assert(0);
            }
            vxlan_unbind_port_vlan(node->vlan_name, port);
            shash_find_and_delete(&vxlan->ports, port->name);
            VLOG_INFO("Deleted port %s from global hash", port->name);
        }
    }
}
