/* Tunnel CLI commands
 *
 * Copyright (C) 2016 Hewlett Packard Enterprise Development LP
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * File: tunnel_vty.c
 *
 * Purpose:  To add tunnel CLI configuration and display commands.
 */

#include <inttypes.h>
#include <netdb.h>
#include <pwd.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/un.h>
#include <setjmp.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <lib/version.h>
#include "command.h"
#include "tunnel_vty.h"
#include "memory.h"
#include "prefix.h"
#include "openvswitch/vlog.h"
#include "openswitch-idl.h"
#include "ovsdb-idl.h"
#include "smap.h"
#include "vswitch-idl.h"
#include "vtysh/vtysh.h"
#include "vtysh/vtysh_user.h"
#include "vtysh/vtysh_ovsdb_if.h"
#include "vtysh/vtysh_ovsdb_config.h"
#include "vtysh/utils/intf_vtysh_utils.h"
#include "vtysh_ovsdb_tunnel_context.h"
#include "vtysh/utils/tunnel_vtysh_utils.h"
#include "vrf-utils.h"

VLOG_DEFINE_THIS_MODULE(vtysh_tunnel_cli);

extern struct ovsdb_idl *idl;
extern struct cmd_element cli_intf_mtu_cmd;
extern struct cmd_element no_cli_intf_mtu_cmd;

extern struct cmd_element cli_intf_shutdown_cmd;
extern struct cmd_element no_cli_intf_shutdown_cmd;
extern int cli_show_interface_exec(struct cmd_element *self, struct vty *vty,
                                   int flags, int argc, const char *argv[],
                                   bool brief, bool tunnel_only);

/* Helper functions */

/* Get the loopback source interface name by interface */
char *
get_source_interface_name(char *intf_name)
{
    char *src_intf_name = NULL;

    src_intf_name = xmalloc(MAX_INTF_LENGTH * sizeof(char));
    memset(src_intf_name, 0, MAX_INTF_LENGTH * sizeof(char));
    snprintf(src_intf_name, MAX_INTF_LENGTH, "%s%s","loopback", intf_name);

    return src_intf_name;
}

/* Get the vlan name by vlan number */
char *
get_vlan_name(char *name)
{
    char *vlan_name = NULL;

    vlan_name = xmalloc(MAX_VLAN_LENGTH * sizeof(char));
    memset(vlan_name, 0, MAX_VLAN_LENGTH * sizeof(char));
    snprintf(vlan_name, MAX_VLAN_LENGTH, "%s%s","VLAN", name);

    return vlan_name;
}

/* Retreive the Logical_Switch row for given tunnel_key */
const struct ovsrec_logical_switch *
get_logical_switch_by_vni(int64_t vni)
{
    const struct ovsrec_logical_switch *ls_row = NULL;
    OVSREC_LOGICAL_SWITCH_FOR_EACH(ls_row, idl)
    {
        if (ls_row->tunnel_key == vni)
        {
            return ls_row;
        }
    }
    return NULL;
}

/* Retreive the Interface row for given interface name */
const struct ovsrec_interface *
get_interface_by_name(char *tunnel_name)
{
    const struct ovsrec_interface *intf_row = NULL;
    OVSREC_INTERFACE_FOR_EACH(intf_row, idl)
    {
        if (strcmp(intf_row->name, tunnel_name) == 0)
        {
            return intf_row;
        }
    }
    return NULL;
}

/* Retreive the Port row for given port name */
const struct ovsrec_port *
get_port_by_name(char *tunnel_name)
{
    const struct ovsrec_port *port_row = NULL;
    OVSREC_PORT_FOR_EACH(port_row, idl)
    {
        if (strcmp(port_row->name, tunnel_name) == 0)
        {
            return port_row;
        }
    }
    return NULL;
}

/* Retreive the VLAN row for given vlan name */
const struct ovsrec_vlan *
get_vlan_by_name(char *vlan_name)
{
    const struct ovsrec_vlan *vlan_row = NULL;
    OVSREC_VLAN_FOR_EACH(vlan_row, idl)
    {
        if (strcmp(vlan_row->name, vlan_name) == 0)
        {
            return vlan_row;
        }
    }
    return NULL;
}

/* Retreive the Bridge row for given bridge name */
const struct ovsrec_bridge *
get_default_bridge()
{
    const struct ovsrec_bridge *br_row = NULL;
    OVSREC_BRIDGE_FOR_EACH(br_row, idl)
    {
        if (strcmp(br_row->name, DEFAULT_BRIDGE_NAME) == 0)
        {
            return br_row;
        }
    }
    return NULL;
}

/*
 * Returns CMD_SUCCESS if transaction is successful.
 * Logs OVSDB_TXN_COMMIT_ERROR when transaction commit fails
 * and returns CMD_OVSDB_FAILURE.
*/
int
txn_status_and_log(const int txn_status)
{
    int status = CMD_OVSDB_FAILURE;
    if (txn_status == TXN_SUCCESS || txn_status == TXN_UNCHANGED)
    {
        status = CMD_SUCCESS;
    }
    else
    {
        VLOG_ERR("Transaction commit failure in %s:%d ", __func__, __LINE__);
        VLOG_ERR(OVSDB_TXN_COMMIT_ERROR);
    }

    return status;
}

/*
 * Adds a new port row in the Port table and
 * port reference in 'ports' column in the Bridge table
 */
const struct ovsrec_port *
add_port_reference_in_bridge(struct ovsdb_idl_txn *tunnel_txn,
                              char *tunnel_name,
                              const struct ovsrec_bridge *default_bridge_row)
{
    struct ovsrec_port **ports = NULL;
    const struct ovsrec_port *port_row = NULL;

    port_row = ovsrec_port_insert(tunnel_txn);
    ovsrec_port_set_name(port_row, tunnel_name);
    ports = xmalloc(sizeof *default_bridge_row->ports *
                   (default_bridge_row->n_ports + 1));
    for (int i = 0; i < default_bridge_row->n_ports; i++)
    {
        ports[i] = default_bridge_row->ports[i];
    }

    ports[default_bridge_row->n_ports] = CONST_CAST(struct ovsrec_port*,
                                                    port_row);
    ovsrec_bridge_set_ports(default_bridge_row, ports,
                            default_bridge_row->n_ports + 1);
    free(ports);

    return port_row;
}

/*
 * Adds a new port and adds the appropriate references for a VxLAN tunnel
 */
const struct ovsrec_port *
add_vxlan_port_reference(struct ovsdb_idl_txn *tunnel_txn, char *tunnel_name)
{
    const struct ovsrec_bridge *default_bridge_row = get_default_bridge();
    if (default_bridge_row == NULL)
    {
        VLOG_DBG("Couldn't fetch default Bridge row. %s:%d",
                 __func__, __LINE__);
        return NULL;
    }

    return add_port_reference_in_bridge(tunnel_txn, tunnel_name,
                                        default_bridge_row);
}

/*
 * Adds a new port and reference in the default VRF.
 */
const struct ovsrec_port *
add_port_reference_in_vrf(struct ovsdb_idl_txn *tunnel_txn,
                          char *tunnel_name,
                          const struct ovsrec_vrf *default_vrf_row)
{
    struct ovsrec_port **ports = NULL;
    const struct ovsrec_port *port_row = NULL;

    port_row = ovsrec_port_insert(tunnel_txn);
    ovsrec_port_set_name(port_row, tunnel_name);
    ports = xmalloc(sizeof *default_vrf_row->ports *
                    (default_vrf_row->n_ports + 1));

    for (int i = 0; i < default_vrf_row->n_ports; i++)
    {
        ports[i] = default_vrf_row->ports[i];
    }

    ports[default_vrf_row->n_ports] = CONST_CAST(struct ovsrec_port*,
                                                 port_row);
    ovsrec_vrf_set_ports(default_vrf_row, ports,
                         default_vrf_row->n_ports + 1);
    free(ports);

    return port_row;
}

/*
 * Adds a new port and adds the appropriate references for a GRE tunnel
 */
const struct ovsrec_port *
add_gre_port_reference(struct ovsdb_idl_txn *tunnel_txn, char *tunnel_name)
{
    const struct ovsrec_vrf *default_vrf_row = get_default_vrf(idl);
    if (default_vrf_row == NULL)
    {
        VLOG_DBG("Couldn't fetch default VRF row. %s:%d",
                 __func__, __LINE__);
        return NULL;
    }

    return add_port_reference_in_vrf(tunnel_txn, tunnel_name, default_vrf_row);
}

/*
 * Adds a new interface row in the Interface table and
 * interface reference in 'interfaces' column in the Port table
 */
void
add_interface_reference_in_port(struct ovsdb_idl_txn *tunnel_txn,
                                 char *tunnel_name,
                                 char *tunnel_mode,
                                 const struct ovsrec_port *port_row)
{
    struct ovsrec_interface **interfaces = NULL;
    const struct ovsrec_interface *intf_row = NULL;
    char *interface_type;
    int i = 0;

    intf_row = ovsrec_interface_insert(tunnel_txn);
    ovsrec_interface_set_name(intf_row, tunnel_name);

    if (!strcmp(tunnel_mode, TUNNEL_MODE_GRE_STR))
    {
        interface_type = OVSREC_INTERFACE_TYPE_GRE_IPV4;
    }
    else
    {
        interface_type = OVSREC_INTERFACE_TYPE_VXLAN;
    }

    ovsrec_interface_set_type(intf_row, interface_type);

    interfaces = xmalloc(sizeof *port_row->interfaces *
                         (port_row->n_interfaces + 1));

    for (i = 0; i < port_row->n_interfaces; i++)
        interfaces[i] = port_row->interfaces[i];

    interfaces[port_row->n_interfaces] = CONST_CAST(struct ovsrec_interface*,
                                                    intf_row);
    ovsrec_port_set_interfaces(port_row, interfaces,
                               port_row->n_interfaces + 1);
    free(interfaces);
}

/*
 * Deletes a port and removes the port reference from the default VRF based on
 * the tunnel name
 */
void
remove_port_reference_from_vrf(struct ovsdb_idl_txn *tunnel_txn,
                               char *tunnel_name,
                               const struct ovsrec_vrf *default_vrf_row)
{
    struct ovsrec_port **port_list = NULL;
    port_list = xmalloc(sizeof *default_vrf_row->ports *
                        (default_vrf_row->n_ports - 1));

    for (int i = 0, j = 0; i < default_vrf_row->n_ports; i++)
    {
        if (strcmp(default_vrf_row->ports[i]->name, tunnel_name) != 0)
        {
            port_list[j++] = default_vrf_row->ports[i];
        }
    }
    ovsrec_vrf_set_ports(default_vrf_row, port_list,
                         default_vrf_row->n_ports - 1);
    free(port_list);
}

/*
 * Deletes a port and
 * removes port reference from 'ports' column in the Bridge table
 */
void
remove_port_reference_from_bridge(
                        struct ovsdb_idl_txn *tunnel_txn,
                        char *tunnel_name,
                        const struct ovsrec_bridge *default_bridge_row)
{
    struct ovsrec_port **port_list = NULL;
    int i = 0, j = 0;

    port_list = xmalloc(sizeof *default_bridge_row->ports *
                        (default_bridge_row->n_ports - 1));
    for (i = 0, j = 0; i < default_bridge_row->n_ports; i++)
    {
        if(strcmp(default_bridge_row->ports[i]->name, tunnel_name) != 0)
            port_list[j++] = default_bridge_row->ports[i];
    }
    ovsrec_bridge_set_ports(default_bridge_row, port_list,
                            default_bridge_row->n_ports - 1);
    free(port_list);
}

/*
 * Deletes an interface and
 * removes interface reference from 'interfaces' column from the Port table
 */
void
remove_interface_reference_from_port(struct ovsdb_idl_txn *tunnel_txn,
                                     char *tunnel_name,
                                     const struct ovsrec_port *port_row)
{
    struct ovsrec_interface **interface_list = NULL;
    struct ovsrec_interface *intf_row = NULL;
    int i = 0, j = 0;

    interface_list = xmalloc(sizeof *port_row->interfaces *
                             (port_row->n_interfaces - 1));
    for (i = 0, j = 0; i < port_row->n_interfaces; i++)
    {
        if(strcmp(port_row->interfaces[i]->name, tunnel_name) != 0)
            interface_list[j++] = port_row->interfaces[i];
        else
            intf_row = port_row->interfaces[i];
    }

    ovsrec_port_set_interfaces(port_row, interface_list,
                               port_row->n_interfaces - 1);
    ovsrec_interface_delete(intf_row);

    free(interface_list);
}

/*
 * Deletes a port and removes the appropriate references for a GRE tunnel
 */
int
remove_gre_port_reference(struct ovsdb_idl_txn *tunnel_txn, char *tunnel_name)
{
    const struct ovsrec_vrf *default_vrf_row = get_default_vrf(idl);
    if (default_vrf_row == NULL)
    {
        VLOG_DBG("Couldn't fetch default VRF row. %s:%d",
                 __func__, __LINE__);
        return CMD_OVSDB_FAILURE;
    }

    remove_port_reference_from_vrf(tunnel_txn, tunnel_name, default_vrf_row);
    return CMD_SUCCESS;
}

/*
 * Deletes a port and removes the appropriate references for a VxLAN tunnel
 */
int
remove_vxlan_port_reference(struct ovsdb_idl_txn *tunnel_txn, char *tunnel_name)
{
    const struct ovsrec_bridge *default_bridge_row = get_default_bridge();
    if (default_bridge_row == NULL)
    {
        VLOG_DBG("Couldn't fetch default bridge row. %s:%d",
                 __func__, __LINE__);
        return CMD_OVSDB_FAILURE;
    }

    remove_port_reference_from_bridge(tunnel_txn, tunnel_name,
                                      default_bridge_row);
    return CMD_SUCCESS;
}

/*
 * Adds vlan_to_vni binding in 'vlan_tunnel_keys' column in the Port table
 * for given vlan and tunnel_key
 */
void
add_vlan_to_vni_binding_in_port(
                            const struct ovsrec_port *port_row,
                            const struct ovsrec_vlan *vlan_row,
                            const struct ovsrec_logical_switch *ls_row)
{
    struct ovsrec_vlan **vlan_list = NULL;
    struct ovsrec_logical_switch **tunnel_key_list = NULL;
    int i = 0;

    vlan_list = xmalloc(sizeof *port_row->key_vlan_tunnel_keys *
                   (port_row->n_vlan_tunnel_keys + 1));
    for (i = 0; i < port_row->n_vlan_tunnel_keys; i++)
        vlan_list[i] = port_row->key_vlan_tunnel_keys[i];

    vlan_list[port_row->n_vlan_tunnel_keys] =
        CONST_CAST(struct ovsrec_vlan*, vlan_row);

    tunnel_key_list = xmalloc(sizeof *port_row->value_vlan_tunnel_keys *
                   (port_row->n_vlan_tunnel_keys + 1));
    for (i = 0; i < port_row->n_vlan_tunnel_keys; i++)
        tunnel_key_list[i] = port_row->value_vlan_tunnel_keys[i];

    tunnel_key_list[port_row->n_vlan_tunnel_keys] =
        CONST_CAST(struct ovsrec_logical_switch*, ls_row);

    ovsrec_port_set_vlan_tunnel_keys(port_row, vlan_list, tunnel_key_list,
                                     (port_row->n_vlan_tunnel_keys + 1));

    free(vlan_list);
    free(tunnel_key_list);
}

/*
 * Removes vlan_to_vni binding from 'vlan_tunnel_keys' column in the Port table
 * for given vlan and tunnel_key
 */
void
remove_vlan_to_vni_binding_in_port(
                            const struct ovsrec_port *port_row,
                            const struct ovsrec_vlan *vlan_row,
                            const struct ovsrec_logical_switch *ls_row)
{
    struct ovsrec_vlan **vlans = NULL;
    struct ovsrec_logical_switch **tunnel_keys = NULL;
    int i = 0, j = 0;

    vlans = xmalloc(sizeof *port_row->key_vlan_tunnel_keys *
                   (port_row->n_vlan_tunnel_keys - 1));
    tunnel_keys = xmalloc(sizeof *port_row->value_vlan_tunnel_keys *
                   (port_row->n_vlan_tunnel_keys + 1));

    for (i = 0, j = 0; i < port_row->n_vlan_tunnel_keys; i++)
    {
        if (strcmp(vlan_row->name, port_row->key_vlan_tunnel_keys[i]->name) == 0)
            continue;
        else
        {
            vlans[j++] = port_row->key_vlan_tunnel_keys[i];
            tunnel_keys[j++] = port_row->value_vlan_tunnel_keys[i];
        }
    }

    ovsrec_port_set_vlan_tunnel_keys(port_row, vlans, tunnel_keys,
                                     port_row->n_vlan_tunnel_keys - 1);

    free(vlans);
    free(tunnel_keys);

}

/*
 * Sets the value for an OVSDB interface row's 'option' column. NULL new value
 * causes the value for the 'option' to be removed. Returns the status of the
 * command.
 */
int
set_intf_option(const struct ovsrec_interface *if_row, const char *option,
                const char *new_value)
{
    enum ovsdb_idl_txn_status txn_status;

    // Update the option value if it is different
    const char *curr_value = smap_get(&if_row->options, option);

    // Value exists and is the same, or if it is already unset and trying to
    // unset again then skip configuration
    if ((!curr_value && !new_value) ||
        (curr_value && new_value && !strcmp(new_value, curr_value)))
    {
        VLOG_DBG("Skip configuration since option values are identical.");
        return CMD_SUCCESS;
    }

    struct ovsdb_idl_txn *txn = cli_do_config_start();
    if (txn == NULL)
    {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(txn);
        return CMD_OVSDB_FAILURE;
    }

    // Clone any existing options and update value for the option
    struct smap if_options;
    smap_clone(&if_options, &if_row->options);

    new_value ? smap_replace(&if_options, option, new_value) :
                smap_remove(&if_options, option);

    ovsrec_interface_set_options(if_row, &if_options);

    txn_status = cli_do_config_finish(txn);
    smap_destroy(&if_options);

    return txn_status_and_log(txn_status);
}

/*
 * Sets the OVSDB interface row's IP address based on the tunnel's name.
 * Returns the status of the command.
 */
int
set_intf_tunnel_ip_addr(struct vty *vty,
                        char *tunnel_name,
                        const char *new_ip)
{
    const struct ovsrec_interface *if_row = NULL;
    const struct ovsrec_port *port_row = NULL;
    enum ovsdb_idl_txn_status txn_status;

    if_row = get_interface_by_name(tunnel_name);
    if (!if_row)
    {
        vty_out(vty, "%% Invalid tunnel interface %s%s",
                tunnel_name, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    // Set the IP address for the interface
    port_row = get_port_by_name(tunnel_name);
    if (!port_row)
    {
        vty_out(vty, "%% Port %s not found.%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_SUCCESS;
    }

    if (new_ip && !is_valid_ip_address(new_ip))
    {
        vty_out(vty, "%% Malformed IP address %s", VTY_NEWLINE);
        return CMD_WARNING;
    }

    struct ovsdb_idl_txn *txn = cli_do_config_start();
    if (txn == NULL)
    {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(txn);
        return CMD_OVSDB_FAILURE;
    }

    ovsrec_port_set_ip4_address(port_row, new_ip);
    txn_status = cli_do_config_finish(txn);

    return txn_status_and_log(txn_status);
}

/*
 * Sets the OVSDB interface row's source IP address. Returns the
 * command status.
 */
int
set_intf_src_ip(struct vty *vty, const struct ovsrec_interface *if_row,
                const char *new_ip)
{
    const char *src_if = NULL;

    // Check if IP is supposed to be set by configured interface
    src_if = smap_get(&if_row->options,
                      OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_INTF);

    if (src_if)
    {
        vty_out(vty, "%% Source Interface IP %s is already set %s",
                src_if, VTY_NEWLINE);
        return CMD_SUCCESS;
    }

    if (!is_valid_ip_address(new_ip))
    {
        vty_out(vty, "%% Malformed IP address %s", VTY_NEWLINE);
        return CMD_WARNING;
    }

    return set_intf_option(if_row, OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_IP,
                           new_ip);
}

/*
 * Removes the OVSDB interface row's source IP.
 */
int
unset_intf_src_ip(const struct ovsrec_interface *if_row)
{
    return set_intf_option(if_row, OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_IP,
                           NULL);
}

/*
 * Sets the OVSDB interface row's destination IP.
 */
int
set_intf_dest_ip(const struct ovsrec_interface *if_row, const char *new_ip)
{
    return set_intf_option(if_row, OVSREC_INTERFACE_OPTIONS_REMOTE_IP, new_ip);
}

/*
 * Removes the OVSDB interface row's destination IP.
 */
int
unset_intf_dest_ip(const struct ovsrec_interface *if_row)
{
    return set_intf_option(if_row, OVSREC_INTERFACE_OPTIONS_REMOTE_IP, NULL);
}

/*
 * Sets the OVSDB interface row's source interface configuration.
 */
int
set_src_intf(struct vty *vty, const struct ovsrec_interface *if_row,
             char *new_if)
{
    const char *src_ip = NULL;

    // Check if IP is already configured
    src_ip = smap_get(&if_row->options,
                      OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_IP);

    if (src_ip)
    {
        vty_out(vty, "%% Source IP %s is already set %s", src_ip, VTY_NEWLINE);
        return CMD_SUCCESS;
    }

    if (new_if && !get_interface_by_name(new_if))
    {
        vty_out(vty, "%% Interface %s does not exist %s", new_if, VTY_NEWLINE);
        return CMD_WARNING;
    }

    return set_intf_option(if_row, OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_INTF,
                           new_if);
}

/*
 * Removes the OVSDB interface row's source interface configuration.
 */
int
unset_src_intf(const struct ovsrec_interface *if_row)
{
    return set_intf_option(if_row, OVSREC_INTERFACE_OPTIONS_TUNNEL_SOURCE_INTF,
                           NULL);
}

/* Add new tunnel_key to 'vni_list' in 'options' column of Interface Table */
char * add_vni_to_vni_list(const char *cur_vni_list, const char *vni)
{
    char *new_vni_list = NULL;
    char *temp_vni_list = NULL;
    char *copy_vni_list = NULL;
    char *vni_str = NULL;
    bool vni_found  = false;

    copy_vni_list = xmalloc(sizeof(int64_t) * sizeof(char) *
                            TUNNEL_MAX_VNIS_IN_VNI_LIST);
    memset(copy_vni_list, 0, sizeof(int64_t) * sizeof(char) *
           TUNNEL_MAX_VNIS_IN_VNI_LIST);
    snprintf(copy_vni_list, sizeof(int64_t) * sizeof(char) *
             TUNNEL_MAX_VNIS_IN_VNI_LIST, "%s", cur_vni_list);

    new_vni_list = xmalloc(sizeof(int64_t) * sizeof(char) *
                           TUNNEL_MAX_VNIS_IN_VNI_LIST);
    memset(new_vni_list, 0, sizeof(int64_t) * sizeof(char) *
           TUNNEL_MAX_VNIS_IN_VNI_LIST);

    temp_vni_list = CONST_CAST(char *, cur_vni_list);

    /*
     * Check if there are already any vnis supported on tunnel interface.
     * If the vni is already existent in the vni_list, no changes required.
     * If the vni is not found in the vni_list append the new vni in the end.
     * example of vni_list: "50 60 70".
     */
    if (temp_vni_list != NULL) {
        vni_str = strtok(temp_vni_list, " ");
        if (vni_str && (strcmp(vni_str, vni) != 0)) {
            strcpy(new_vni_list, vni_str);
            while ((vni_str = strtok(NULL, " ")) != NULL) {
                if (strcmp(vni_str, vni) != 0) {
                    strcat(new_vni_list, " ");
                    strcat(new_vni_list, vni_str);
                } else {
                    vni_found = true;
                    break;
                }
            }
        } else {
            vni_found = true;
        }

        if (!vni_found) {
            strcat(new_vni_list, " ");
            strcat(new_vni_list, vni);
        } else {
            memcpy(new_vni_list, copy_vni_list, strlen(copy_vni_list));
        }
    } else {
        memcpy(new_vni_list, vni, strlen(vni));
    }

    new_vni_list[strlen(new_vni_list)] = '\0';
    free(copy_vni_list);

    return new_vni_list;
}

/* Remove new tunnel_key to 'vni_list' in 'options' column of Interface Table */
char * remove_vni_to_vni_list(const char *cur_vni_list, const char *vni)
{
    char *temp_vni_list = NULL;
    char *new_vni_list = NULL;
    char *vni_str = NULL;
    bool first_vni_found = false;

    new_vni_list = xmalloc(sizeof(int64_t) * sizeof(char) *
                           TUNNEL_MAX_VNIS_IN_VNI_LIST);
    memset(new_vni_list, 0, sizeof(int64_t) * sizeof(char) *
           TUNNEL_MAX_VNIS_IN_VNI_LIST);

    temp_vni_list = CONST_CAST(char *, cur_vni_list);

    /*
     * Find the given vni in the 'vni_list' inside 'options' in Interface table.
     * If the vni found in the vni_list, skip it and do not add to new_vni_list.
     * If the vni is not found in the vni_list, no changes required.
     */
    if(temp_vni_list != NULL) {
        vni_str = strtok(temp_vni_list, " ");
        if (strcmp(vni_str, vni) != 0)
            snprintf(new_vni_list, sizeof(int64_t) * sizeof(char) *
                     TUNNEL_MAX_VNIS_IN_VNI_LIST, "%s", vni_str);
        else
            first_vni_found = true;

        while ((vni_str = strtok(NULL, " ")) != NULL) {
            if (!first_vni_found) {
                if (strcmp(vni_str, vni) != 0) {
                    strcat(new_vni_list, " ");
                    strcat(new_vni_list, vni_str);
                }
            } else {
                strcat(new_vni_list, vni_str);
                strcat(new_vni_list, " ");
            }
        }
    }
    if (new_vni_list[strlen(new_vni_list) - 1] == ' ')
        new_vni_list[strlen(new_vni_list) - 1] = '\0';
    else
        new_vni_list[strlen(new_vni_list)] = '\0';

    return new_vni_list;
}

/* Create an interface tunnel with vxlan mode */
DEFUN (cli_create_tunnel,
       cli_create_tunnel_cmd,
       "interface tunnel " TUNNEL_INTF_RANGE " mode (vxlan)",
       INTERFACE_STR
       TUNNEL_STR
       TUNNEL_NUM_HELP_STR
       TUNNEL_MODE_HELP_STR
       TUNNEL_MODE_VXLAN_HELP_STR)
{
    const struct ovsrec_interface *intf_row = NULL;
    const struct ovsrec_port *port_row = NULL;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    static char tunnel_name[MAX_TUNNEL_LENGTH] = {0};
    char *tunnel_mode = CONST_CAST(char*, argv[1]);
    int tunnel_node;

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s%s","tunnel", argv[0]);
    VLOG_DBG("tunnel_name %s\n", tunnel_name);

    intf_row = get_interface_by_name(tunnel_name);

    if (intf_row)
    {
        vty_out(vty, "%% Tunnel %s already exists...Please don't provide "
                "tunnel mode. %s", tunnel_name, VTY_NEWLINE);
        return CMD_WARNING;
    }

    tunnel_txn = cli_do_config_start();
    if (tunnel_txn == NULL)
    {
        VLOG_DBG("Transaction creation failed by %s. %s:%d",
                 " cli_do_config_start()", __func__, __LINE__);
        cli_do_config_abort(tunnel_txn);
        return CMD_OVSDB_FAILURE;
    }

    if (strcmp(tunnel_mode, OVSREC_INTERFACE_TYPE_VXLAN) == 0)
    {
        port_row = add_vxlan_port_reference(tunnel_txn, tunnel_name);
        tunnel_node = VXLAN_TUNNEL_INTERFACE_NODE;
    }
    else
    {
        port_row = add_gre_port_reference(tunnel_txn, tunnel_name);
        tunnel_node = GRE_TUNNEL_INTERFACE_NODE;
    }

    if (!port_row)
    {
        VLOG_ERR("Failed to add port reference");
        cli_do_config_abort(tunnel_txn);
        return CMD_OVSDB_FAILURE;
    }

    /*
     * Add interface reference in the Port after adding new
     * interface.
     */
    add_interface_reference_in_port(tunnel_txn, tunnel_name,
                                    tunnel_mode, port_row);

    status_txn = cli_do_config_finish(tunnel_txn);
    if (status_txn != TXN_SUCCESS && status_txn != TXN_UNCHANGED)
    {
        VLOG_ERR("Transaction commit failed in function=%s, line=%d",
                 __func__, __LINE__);
        return CMD_OVSDB_FAILURE;
    }

    vty->node = tunnel_node;
    vty->index = (void *)tunnel_name;

    return CMD_SUCCESS;
}

DEFUN (cli_update_tunnel,
       cli_update_tunnel_cmd,
       "interface tunnel " TUNNEL_INTF_RANGE,
       INTERFACE_STR
       TUNNEL_STR
       TUNNEL_NUM_HELP_STR)
{
    const struct ovsrec_interface *intf_row = NULL;
    static char tunnel_name[MAX_TUNNEL_LENGTH] = {0};
    int tunnel_node;

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s%s","tunnel", argv[0]);
    intf_row = get_interface_by_name(tunnel_name);

    if (intf_row)
    {
        if (strcmp(intf_row->type, OVSREC_INTERFACE_TYPE_VXLAN) == 0)
        {
            tunnel_node = VXLAN_TUNNEL_INTERFACE_NODE;
        }
        else
        {
            tunnel_node = GRE_TUNNEL_INTERFACE_NODE;
        }
    }
    else
    {
        vty_out(vty, "%% Tunnel %s does not exist.%s", tunnel_name,
                VTY_NEWLINE);
        return CMD_WARNING;
    }

    vty->node = tunnel_node;
    vty->index = (void *)tunnel_name;

    return CMD_SUCCESS;
}

/* Create an interface tunnel with gre ipv4 mode */
ALIAS (cli_create_tunnel,
       cli_create_gre_tunnel_cmd,
       "interface tunnel " TUNNEL_INTF_RANGE " mode (gre) (ipv4)",
       INTERFACE_STR
       TUNNEL_STR
       TUNNEL_NUM_HELP_STR
       TUNNEL_MODE_HELP_STR
       TUNNEL_MODE_GRE_HELP_STR
       "IPv4 mode for tunneling\n")

/* Deletes the interface tunnel with given tunnel_name */
DEFUN (cli_delete_tunnel,
       cli_delete_tunnel_cmd,
       "no interface tunnel " TUNNEL_INTF_RANGE,
       NO_STR
       INTERFACE_STR
       TUNNEL_STR
       "Delete a tunnel interface\n")
{
    const struct ovsrec_interface *intf_row = NULL;
    const struct ovsrec_port *port_row = NULL;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char tunnel_name[MAX_TUNNEL_LENGTH] = {0};
    int status;

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s%s","tunnel", argv[0]);

    VLOG_DBG("tunnel_name %s\n", tunnel_name);

    intf_row = get_interface_by_name(tunnel_name);
    port_row = get_port_by_name(tunnel_name);

    if(intf_row)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        if (strcmp(intf_row->type, OVSREC_INTERFACE_TYPE_VXLAN) == 0)
        {
            status = remove_vxlan_port_reference(tunnel_txn, tunnel_name);
        }
        else
        {
            status = remove_gre_port_reference(tunnel_txn, tunnel_name);
        }

        if (status != CMD_SUCCESS)
        {
            assert(0);
            VLOG_DBG("Failed to remove references for the tunnel. %s:%d",
                    __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        /*
         * Remove an interface reference in the Port after adding new
         * interface.
         */
        remove_interface_reference_from_port(tunnel_txn,
                                             tunnel_name,
                                             port_row);

        ovsrec_port_delete(port_row);

        status_txn = cli_do_config_finish(tunnel_txn);
        if (status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "%% Can't delete tunnel %s as it doesn't exist %s",
                tunnel_name, VTY_NEWLINE);
        return CMD_WARNING;
    }

    return CMD_SUCCESS;
}

DEFUN (cli_set_tunnel_ip,
       cli_set_tunnel_ip_cmd,
       "ip address (A.B.C.D/M|X:X::X:X/M)",
       IP_STR
       IP_STR
       "Set the tunnel IP\n")
{
    return set_intf_tunnel_ip_addr(vty, (char*)vty->index, (char*)argv[0]);
}

DEFUN (cli_no_set_tunnel_ip,
       cli_no_set_tunnel_ip_val_cmd,
       "no ip address (A.B.C.D/M|X:X::X:X/M)",
       NO_STR
       IP_STR
       IP_STR
       "Remove the tunnel IP\n")
{
    return set_intf_tunnel_ip_addr(vty, (char*)vty->index, NULL);
}

ALIAS (cli_no_set_tunnel_ip,
       cli_no_set_tunnel_ip_cmd,
       "no ip address",
       NO_STR
       IP_STR
       IP_STR
       "Remove the tunnel IP\n")

DEFUN (cli_set_source_intf,
       cli_set_source_intf_cmd,
       "source-interface loopback <1-2147483647>",
       TUNNEL_SOURCE_IF_HELP_STR
       TUNNEL_LOOPBACK_IF_HELP_STR
       TUNNEL_LOOPBACK_IF_NUM_HELP_STR)
{
    char *src_intf_name = NULL;

    if (vty->node == VXLAN_TUNNEL_INTERFACE_NODE)
    {
        src_intf_name = get_source_interface_name(CONST_CAST(char*, argv[0]));
    }
    else
    {
        src_intf_name = CONST_CAST(char*, argv[0]);
    }

    const struct ovsrec_interface *if_row = NULL;

    if_row = get_interface_by_name((char*)vty->index);
    if (!if_row)
    {
        vty_out(vty, "%% Invalid tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return set_src_intf(vty, if_row, src_intf_name);
}

DEFUN (cli_no_set_source_intf,
       cli_no_set_source_intf_cmd,
       "no source-interface loopback <1-2147483647>",
       NO_STR
       TUNNEL_NO_SOURCE_IF_HELP_STR
       TUNNEL_LOOPBACK_IF_HELP_STR
       TUNNEL_LOOPBACK_IF_NUM_HELP_STR)
{
    const struct ovsrec_interface *if_row = NULL;
    const struct ovsrec_interface *if_loopback_row = NULL;
    char *src_if_name = NULL;

    if_row = get_interface_by_name((char*)vty->index);
    if (!if_row)
    {
        vty_out(vty, "%% Invalid tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    src_if_name = get_source_interface_name(CONST_CAST(char*, argv[0]));
    if_loopback_row = get_interface_by_name(src_if_name);
    free(src_if_name);

    if (!if_loopback_row)
    {
        vty_out(vty, "%% Can't remove the source interface as given loopback "
                "%s doesn't exist %s", argv[0], VTY_NEWLINE);
        return CMD_WARNING;
    }

    return unset_src_intf(if_row);
}

DEFUN (cli_set_gre_source_intf,
       cli_set_gre_source_intf_cmd,
       "source interface IFNUMBER",
       TUNNEL_SOURCE_HELP_STR
       "Select an interface\n"
       "Interface number\n")
{
    const struct ovsrec_interface *if_row = NULL;

    if_row = get_interface_by_name((char*)vty->index);
    if (!if_row)
    {
        vty_out(vty, "%% Invalid GRE tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return set_src_intf(vty, if_row, CONST_CAST(char*, argv[0]));
}

DEFUN (cli_no_gre_source_intf,
       cli_no_gre_source_intf_cmd,
       "no source interface",
       NO_STR
       TUNNEL_SOURCE_HELP_STR
       "Remove the source interface\n")
{
    const struct ovsrec_interface *if_row = NULL;

    if_row = get_interface_by_name((char*)vty->index);
    if (!if_row)
    {
        vty_out(vty, "%% Invalid GRE tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return unset_src_intf(if_row);
}

ALIAS (cli_no_gre_source_intf,
       cli_no_gre_source_intf_val_cmd,
       "no source interface <1-2147483647>",
       NO_STR
       TUNNEL_SOURCE_HELP_STR
       INTERFACE_STR
       "Remove the source interface\n")

DEFUN (cli_set_source_ip,
       cli_set_source_ip_cmd,
       "source ip (A.B.C.D|X:X::X:X)",
       TUNNEL_SOURCE_HELP_STR
       IP_STR
       TUNNEL_SOURCE_IP_HELP_STR)
{
    const struct ovsrec_interface *if_row = NULL;

    if_row = get_interface_by_name((char*)vty->index);
    if (!if_row)
    {
        vty_out(vty, "%% Invalid tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return set_intf_src_ip(vty, if_row, argv[0]);
}

DEFUN (cli_no_set_source_ip,
       cli_no_set_source_ip_val_cmd,
       "no source ip (A.B.C.D|X:X::X:X)",
       NO_STR
       TUNNEL_SOURCE_HELP_STR
       IP_STR
       TUNNEL_NO_SOURCE_IP_HELP_STR)
{
    const struct ovsrec_interface *if_row = NULL;

    if_row = get_interface_by_name((char*)vty->index);
    if (!if_row)
    {
        vty_out(vty, "%% Invalid tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return unset_intf_src_ip(if_row);
}

ALIAS (cli_no_set_source_ip,
       cli_no_set_source_ip_cmd,
       "no source ip",
       NO_STR
       TUNNEL_SOURCE_HELP_STR
       TUNNEL_NO_SOURCE_IP_HELP_STR)

DEFUN (cli_set_dest_ip,
       cli_set_dest_ip_cmd,
       "destination ip (A.B.C.D|X:X::X:X)",
       TUNNEL_DEST_HELP_STR
       IP_STR
       "Set the destination IP\n")
{
    const struct ovsrec_interface *if_row = NULL;

    if_row = get_interface_by_name((char*)vty->index);
    if (!if_row)
    {
        vty_out(vty, "%% Invalid tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return set_intf_option(if_row, OVSREC_INTERFACE_OPTIONS_REMOTE_IP,
                           argv[0]);
}

DEFUN (cli_no_set_dest_ip,
       cli_no_set_dest_ip_val_cmd,
       "no destination ip (A.B.C.D|X:X::X:X)",
       NO_STR
       TUNNEL_DEST_HELP_STR
       IP_STR
       TUNNEL_NO_DEST_IP_HELP_STR)
{
    const struct ovsrec_interface *if_row = NULL;

    if_row = get_interface_by_name((char*)vty->index);
    if (!if_row)
    {
        vty_out(vty, "%% Invalid tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return unset_intf_dest_ip(if_row);
}

ALIAS (cli_no_set_dest_ip,
       cli_no_set_dest_ip_cmd,
       "no destination ip",
       NO_STR
       TUNNEL_DEST_HELP_STR
       TUNNEL_NO_DEST_IP_HELP_STR)

static int
set_vxlan_tunnel_key(int64_t vni_id)
{
    const struct ovsrec_system *system_row = NULL;
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    const struct ovsrec_bridge *bridge_row = NULL;
    enum ovsdb_idl_txn_status txn_status;
    int status;
    struct ovsdb_idl_txn *txn = cli_do_config_start();

    if (txn == NULL) {
        cli_do_config_abort(txn);
        return CMD_OVSDB_FAILURE;
    }

    system_row = ovsrec_system_first(idl);
    if (system_row == NULL) {
        cli_do_config_abort(txn);
        return CMD_SUCCESS;
    }

    bridge_row = ovsrec_bridge_first(idl);

    if (!(get_logical_switch_by_vni(vni_id)))
    {
        logical_switch_row = ovsrec_logical_switch_insert(txn);

        ovsrec_logical_switch_set_tunnel_key(logical_switch_row, vni_id);
        ovsrec_logical_switch_set_bridge(logical_switch_row, bridge_row);
        ovsrec_logical_switch_set_from(logical_switch_row, "hw-vtep");
    }

    txn_status = cli_do_config_finish(txn);
    status = txn_status_and_log(txn_status);

    if (status == CMD_SUCCESS)
    {
        vty->node = VNI_NODE;
        vty->index = (void *)vni_id;
    }

    return status;
}

DEFUN (cli_set_vxlan_tunnel_key,
        cli_set_vxlan_tunnel_key_cmd,
        "vni <1-16777216>",
        VNI_STR
        TUNNEL_VXLAN_VNI_LIST_NUM_HELP_STR
        "Set the tunnel key\n")
{
    int64_t vni_id = (int64_t)(atoi(argv[0]));
    return set_vxlan_tunnel_key(vni_id);
}

static int
no_set_vxlan_tunnel_key(int64_t vni_id)
{
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    enum ovsdb_idl_txn_status txn_status;
    int status;
    struct ovsdb_idl_txn *txn = cli_do_config_start();

    if (txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(txn);
        return CMD_OVSDB_FAILURE;
    }

    logical_switch_row = get_logical_switch_by_vni(vni_id);
    if (logical_switch_row)
        if(logical_switch_row->tunnel_key == vni_id)
        {
            ovsrec_logical_switch_delete(logical_switch_row);
        }
        else
            vty_out(vty,"%% Tunnel with %ld tunnel_key not found %s",
                    vni_id, VTY_NEWLINE);
    else
       vty_out(vty,"%% No tunnel with vni %ld found %s", vni_id, VTY_NEWLINE);

    txn_status = cli_do_config_finish(txn);
    status = txn_status_and_log(txn_status);

    if (status == CMD_SUCCESS)
    {
        vty->node = CONFIG_NODE;
    }

    return status;
}

DEFUN (cli_no_set_vxlan_tunnel_key,
        cli_no_set_vxlan_tunnel_key_cmd,
        "no vni <1-16777216>",
        TUNNEL_STR
        "Remove the vxlan tunnel key\n")
{
    int64_t vni_id = (int64_t)(atoi(argv[0]));
    return no_set_vxlan_tunnel_key(vni_id);
}


static int
set_vxlan_tunnel_name(const char *name)
{
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    enum ovsdb_idl_txn_status txn_status;
    int status;
    struct ovsdb_idl_txn *txn = cli_do_config_start();
    int64_t vni_id = (int64_t)vty->index;

    if (txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(txn);
        return CMD_OVSDB_FAILURE;
    }

    logical_switch_row = get_logical_switch_by_vni(vni_id);
    if (logical_switch_row)
    {
        ovsrec_logical_switch_set_name(logical_switch_row, name);
    }
    else
        vty_out(vty,"%% Logical switch instance with key %ld not found%s",
                        (int64_t)vty->index, VTY_NEWLINE);

    txn_status = cli_do_config_finish(txn);
    status = txn_status_and_log(txn_status);

    if (status == CMD_SUCCESS)
    {
        vty->node = VNI_NODE;
    }

    return status;
}

DEFUN (cli_set_vxlan_tunnel_name,
        cli_set_vxlan_tunnel_name_cmd,
        "name TUNNEL_NAME",
        TUNNEL_STR
        "Set the vxlan tunnel name\n")
{
    return set_vxlan_tunnel_name(argv[0]);
}


static int
unset_vxlan_tunnel_name(const char *name)
{
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    enum ovsdb_idl_txn_status txn_status;
    int64_t vni_id = (int64_t)vty->index;
    struct ovsdb_idl_txn *txn = cli_do_config_start();

    if (txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(txn);
        return CMD_OVSDB_FAILURE;
    }

    logical_switch_row = get_logical_switch_by_vni(vni_id);
    if (logical_switch_row)
    {
        if (logical_switch_row->name)
            if (strncmp(logical_switch_row->name, name, strlen(logical_switch_row->name)) == 0)
                ovsrec_logical_switch_set_name(logical_switch_row, NULL);
            else
                vty_out(vty,"%% Name %s not found in current tunnel config%s", name, VTY_NEWLINE);
        else
            vty_out(vty,"%% Name not configured in current tunnel context%s", VTY_NEWLINE);
    }
    else
    {
        vty_out(vty,"%% Logical switch instance with key %ld not found%s",
                        (int64_t)vty->index, VTY_NEWLINE);
    }

    txn_status = cli_do_config_finish(txn);

    return txn_status_and_log(txn_status);
}

DEFUN (cli_no_set_vxlan_tunnel_name,
        cli_no_set_vxlan_tunnel_name_cmd,
        "no name TUNNEL_NAME",
        TUNNEL_STR
        "Remove the vxlan tunnel name\n")
{
    return unset_vxlan_tunnel_name(argv[0]);
}

static int
set_vxlan_tunnel_description(const char *desc)
{
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    enum ovsdb_idl_txn_status txn_status;
    int status;
    struct ovsdb_idl_txn *txn = cli_do_config_start();

    if (txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(txn);
        return CMD_OVSDB_FAILURE;
    }

    logical_switch_row = get_logical_switch_by_vni((int64_t)vty->index);
    if (logical_switch_row)
        ovsrec_logical_switch_set_description(logical_switch_row, desc);
    else
        vty_out(vty,"%% Logical switch instance with key %ld not found%s",
                        (int64_t)vty->index, VTY_NEWLINE);

    txn_status = cli_do_config_finish(txn);
    status = txn_status_and_log(txn_status);

    if (status == CMD_SUCCESS)
    {
        vty->node = VNI_NODE;
    }

    return status;
}

DEFUN (cli_set_tunnel_description,
        cli_set_tunnel_description_cmd,
        "description TUNNEL_DESCRIPTION",
        TUNNEL_STR
        "Set the vxlan tunnel description\n")
{
    return set_vxlan_tunnel_description(argv[0]);
}


static int
unset_vxlan_tunnel_description(const char *description)
{
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    enum ovsdb_idl_txn_status txn_status;
    struct ovsdb_idl_txn *txn = cli_do_config_start();
    int64_t vni_id = (int64_t)vty->index;

    if (txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(txn);
        return CMD_OVSDB_FAILURE;
    }

    logical_switch_row = get_logical_switch_by_vni(vni_id);
    if (logical_switch_row)
    {
        if (logical_switch_row->description)
            if (strncmp(logical_switch_row->description, description, strlen(logical_switch_row->description)) == 0)
                ovsrec_logical_switch_set_description(logical_switch_row, NULL);
            else
                vty_out(vty,"%% Description %s not found in current tunnel config%s", description, VTY_NEWLINE);
        else
            vty_out(vty,"%% Description not configured in current tunnel context%s", VTY_NEWLINE);
    }
    else
        vty_out(vty,"%% Logical switch instance with key %ld not found%s",
                (int64_t)vty->index, VTY_NEWLINE);

    txn_status = cli_do_config_finish(txn);

    return txn_status_and_log(txn_status);
}

DEFUN (cli_no_set_tunnel_description,
        cli_no_set_tunnel_description_cmd,
        "no description TUNNEL_DESCRIPTION",
        TUNNEL_STR
        "Remove the vxlan tunnel description\n")
{
    return unset_vxlan_tunnel_description(argv[0]);
}

static int
set_mcast_group_ip(const char *mcast_ip)
{
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    enum ovsdb_idl_txn_status txn_status;
    struct ovsdb_idl_txn *txn = cli_do_config_start();

    if (txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(txn);
        return CMD_OVSDB_FAILURE;
    }

    logical_switch_row = get_logical_switch_by_vni((int64_t)vty->index);
    if (logical_switch_row)
        ovsrec_logical_switch_set_mcast_group_ip(logical_switch_row, mcast_ip);
    else
        vty_out(vty,"%% Logical switch instance with key %ld not found%s",
                        (int64_t)vty->index, VTY_NEWLINE);

    txn_status = cli_do_config_finish(txn);

    return txn_status_and_log(txn_status);
}

DEFUN (cli_set_multicast_group_ip,
        cli_set_multicast_group_ip_cmd,
        "mcast-group-ip (A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Set multicast group ip\n")
{
    return set_mcast_group_ip(argv[0]);
}


static int
unset_vxlan_tunnel_mcast_group_ip(const char *mcast_ip)
{
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    enum ovsdb_idl_txn_status txn_status;
    struct ovsdb_idl_txn *txn = cli_do_config_start();
    int64_t vni_id = (int64_t)vty->index;

    if (txn == NULL) {
        VLOG_ERR(OVSDB_TXN_CREATE_ERROR);
        cli_do_config_abort(txn);
        return CMD_OVSDB_FAILURE;
    }

    logical_switch_row = get_logical_switch_by_vni(vni_id);
    if (logical_switch_row)
    {
        if (logical_switch_row->mcast_group_ip)
            if (strncmp(logical_switch_row->mcast_group_ip, mcast_ip, strlen(logical_switch_row->mcast_group_ip)) == 0)
                ovsrec_logical_switch_set_mcast_group_ip(logical_switch_row, NULL);
            else
                vty_out(vty,"%% Mcast group ip %s not found for the current tunnel config%s", mcast_ip, VTY_NEWLINE);
        else
            vty_out(vty,"%% Multicast group ip not configured in current tunnel context%s", VTY_NEWLINE);
    }
    else
    {
        vty_out(vty,"%% Logical switch instance with key %ld not found%s",
                        (int64_t)vty->index, VTY_NEWLINE);
    }

    txn_status = cli_do_config_finish(txn);

    return txn_status_and_log(txn_status);
}

DEFUN (cli_no_set_multicast_group_ip,
        cli_no_set_multicast_group_ip_cmd,
        "no mcast-group-ip (A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Remove the multicast group ip\n")
{
    return unset_vxlan_tunnel_mcast_group_ip(argv[0]);
}

DEFUN (cli_set_replication_group_ips,
        cli_set_replication_group_ips_cmd,
        "replication-group (A.B.C.D|X:X::X:X)...(A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Set replication group ips\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_no_set_replication_group_ips,
        cli_no_set_replication_group_ips_cmd,
        "no replication-group (A.B.C.D|X:X::X:X)...(A.B.C.D|X:X::X:X)",
        TUNNEL_STR
        "Remove the given ip from replication group\n")
{

    return CMD_SUCCESS;
}

DEFUN (cli_set_vlan_to_vni_mapping,
        cli_set_vlan_to_vni_mapping_cmd,
        "vlan <1-4095> vni <1-16777216>",
        VLAN_STR
        VLAN_NUM_HELP_STR
        VNI_STR
        TUNNEL_VXLAN_VNI_LIST_NUM_HELP_STR
        "Set per-port vlan to vni mapping\n")
{
    const struct ovsrec_port *port_row = NULL;
    const struct ovsrec_vlan *vlan_row = NULL;
    const struct ovsrec_logical_switch *ls_row = NULL;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;
    char *vlan_name = NULL;

    tunnel_name = (char *)vty->index;
    vlan_name = get_vlan_name(CONST_CAST(char *, argv[0]));

    port_row = get_port_by_name(tunnel_name);
    ls_row = get_logical_switch_by_vni((int64_t)atoi(argv[1]));
    vlan_row = get_vlan_by_name(vlan_name);
    free(vlan_name);

    if(port_row && ls_row && vlan_row)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        /* Add vlan_to_vni binding in port */
        add_vlan_to_vni_binding_in_port(port_row, vlan_row, ls_row);

        status_txn = cli_do_config_finish(tunnel_txn);

        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "%% Cannot modify vlan to vni mapping."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }
    return CMD_SUCCESS;
}

DEFUN (cli_no_set_vlan_to_vni_mapping,
        cli_no_set_vlan_to_vni_mapping_cmd,
        "no vlan <1-4095> vni <1-16777216>",
        NO_STR
        VLAN_STR
        VLAN_NUM_HELP_STR
        VNI_STR
        TUNNEL_VXLAN_VNI_LIST_NUM_HELP_STR
        "Remove vlan to vni mapping\n")
{
    const struct ovsrec_port *port_row = NULL;
    const struct ovsrec_vlan *vlan_row = NULL;
    const struct ovsrec_logical_switch *ls_row = NULL;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;
    char *vlan_name = NULL;

    tunnel_name = (char *)vty->index;
    vlan_name = get_vlan_name(CONST_CAST(char *, argv[0]));

    port_row = get_port_by_name(tunnel_name);
    ls_row = get_logical_switch_by_vni((int64_t)atoi(argv[1]));
    vlan_row = get_vlan_by_name(vlan_name);
    free(vlan_name);

    if(port_row && ls_row && vlan_row)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        /* Remove vlan_to_vni binding in port */
        remove_vlan_to_vni_binding_in_port(port_row, vlan_row, ls_row);

        status_txn = cli_do_config_finish(tunnel_txn);

        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "%% Cannot modify vlan to vni mapping."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }
    return CMD_SUCCESS;
}

static int
set_global_vlan_to_vni_mapping(int argc, const char **argv)
{
    const struct ovsrec_vlan *vlan_row = NULL;
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    const struct ovsrec_system *system_row = NULL;
    bool vlan_found = false;
    int64_t vlan_id = (int64_t)atoi(argv[0]);
    int64_t vni_id = (int64_t)atoi(argv[1]);
    enum ovsdb_idl_txn_status txn_status;
    struct ovsdb_idl_txn *txn = cli_do_config_start();

    if (txn == NULL) {
        cli_do_config_abort(txn);
        return CMD_OVSDB_FAILURE;
    }

    system_row = ovsrec_system_first(idl);
    if (system_row == NULL) {
        cli_do_config_abort(txn);
        return CMD_SUCCESS;
    }

    OVSREC_VLAN_FOR_EACH(vlan_row, idl)
    {
        if (vlan_row->id == vlan_id)
        {
            vlan_found = true;
            break;
        }
    }

    if (vlan_found)
    {
        logical_switch_row = get_logical_switch_by_vni(vni_id);
        if (logical_switch_row)
            ovsrec_vlan_set_tunnel_key(vlan_row, logical_switch_row);
        else
            printf("Tunnel not found\n");
    }
    else
        printf("vlan not found\n");

    txn_status = cli_do_config_finish(txn);
    return txn_status_and_log(txn_status);
}

DEFUN (cli_set_global_vlan_to_vni_mapping,
        cli_set_global_vlan_to_vni_mapping_cmd,
        "vxlan vlan <1-4094> vni <1-16777216>",
        TUNNEL_STR
        "Global vlan to vni mapping\n")
{
    return set_global_vlan_to_vni_mapping(argc, argv);
}

static int
unset_global_vlan_to_vni_mapping(int argc, const char **argv)
{
    const struct ovsrec_vlan *vlan_row = NULL;
    const struct ovsrec_logical_switch *logical_switch_row = NULL;
    const struct ovsrec_system *system_row = NULL;
    bool vlan_found = false;
    int64_t vlan_id = (int64_t)atoi(argv[0]);
    int64_t vni_id = (int64_t)atoi(argv[1]);
    enum ovsdb_idl_txn_status txn_status;
    struct ovsdb_idl_txn *txn = cli_do_config_start();

    if (txn == NULL) {
        cli_do_config_abort(txn);
        return CMD_OVSDB_FAILURE;
    }

    system_row = ovsrec_system_first(idl);
    if (system_row == NULL) {
        cli_do_config_abort(txn);
        return CMD_SUCCESS;
    }

    logical_switch_row = get_logical_switch_by_vni(vni_id);
    if (logical_switch_row)
    {
        OVSREC_VLAN_FOR_EACH(vlan_row, idl)
        {
            if (vlan_row->id == vlan_id)
            {
                vlan_found = true;
                break;
            }
        }
    }

    if (vlan_found)
    {
        if (vlan_row->tunnel_key->tunnel_key == vni_id)
            ovsrec_vlan_set_tunnel_key(vlan_row, NULL);
        else
           printf("vlan vni pair not found\n");
    }
    else
        printf("vlan not found\n");

    txn_status = cli_do_config_finish(txn);

    return txn_status_and_log(txn_status);
}

DEFUN (cli_no_set_global_vlan_to_vni_mapping,
        cli_no_set_global_vlan_to_vni_mapping_cmd,
        "no vxlan vlan <1-4094> vni <1-16777216>",
        TUNNEL_STR
        "Unset global vlan to vni mapping\n")
{
    return unset_global_vlan_to_vni_mapping(argc, argv);
}

DEFUN (cli_set_vxlan_udp_port,
        cli_set_vxlan_udp_port_cmd,
        "vxlan udp-port <1-65535>",
        TUNNEL_VXLAN_STR
        TUNNEL_VXLAN_PORT_HELP_STR
        TUNNEL_VXLAN_PORT_NUM_HELP_STR
        "Set the vxlan udp-port\n")
{
    struct smap options = SMAP_INITIALIZER(&options);
    const struct ovsrec_interface *intf_row = NULL;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;

    tunnel_name = (char *)vty->index;

    intf_row = get_interface_by_name(tunnel_name);

    if(intf_row)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        smap_clone(&options, &intf_row->options);
        smap_replace(&options, OVSREC_INTERFACE_OPTIONS_VXLAN_UDP_PORT,
                    argv[0]);
        ovsrec_interface_set_options(intf_row, &options);
        smap_destroy(&options);

        status_txn = cli_do_config_finish(tunnel_txn);
        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "%% Cannot modify tunnel destination ip."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }
    return CMD_SUCCESS;
}

DEFUN (cli_no_set_vxlan_udp_port,
        cli_no_set_vxlan_udp_port_cmd,
        "no vxlan udp-port <1-65535>",
        NO_STR
        TUNNEL_VXLAN_STR
        TUNNEL_VXLAN_PORT_HELP_STR
        "Set the vxlan udp-port to default(4789)\n")
{
    struct smap options = SMAP_INITIALIZER(&options);
    const struct ovsrec_interface *intf_row = NULL;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;

    tunnel_name = (char *)vty->index;

    intf_row = get_interface_by_name(tunnel_name);

    if(intf_row)
    {
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        smap_clone(&options, &intf_row->options);
        smap_remove(&options, OVSREC_INTERFACE_OPTIONS_VXLAN_UDP_PORT);
        ovsrec_interface_set_options(intf_row, &options);
        smap_destroy(&options);

        status_txn = cli_do_config_finish(tunnel_txn);
        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "%% Cannot modify tunnel destination ip."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return CMD_SUCCESS;
}

DEFUN (cli_set_vni_list,
        cli_set_vni_list_cmd,
        "vxlan-vni <1-8000>",
        TUNNEL_VXLAN_VNI_LIST_STR
        TUNNEL_VXLAN_VNI_LIST_NUM_HELP_STR
        TUNNEL_VXLAN_VNI_LIST_HELP_STR)
{
    struct smap options = SMAP_INITIALIZER(&options);
    const struct ovsrec_interface *intf_row = NULL;
    const struct ovsrec_logical_switch *ls_row = NULL;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;
    char *new_vni_list = NULL;
    const char *cur_vni_list = NULL;
    int64_t ls_tunnel_key = 0;

    tunnel_name = (char *)vty->index;
    ls_tunnel_key = (int64_t) atoi(argv[0]);

    intf_row = get_interface_by_name(tunnel_name);
    ls_row = get_logical_switch_by_vni(ls_tunnel_key);

    if(intf_row)
    {
        if(!ls_row)
        {
            vty_out(vty, "%% Can't add vni to vni_list as given tunnel_key %ld "
                    "doesn't exist %s", ls_tunnel_key, VTY_NEWLINE);
            return CMD_WARNING;
        }

        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        cur_vni_list = smap_get(&intf_row->options, OVSREC_INTERFACE_OPTIONS_VNI_LIST);

        /* Add new vni to vni_list */
        new_vni_list = add_vni_to_vni_list(cur_vni_list, argv[0]);

        smap_clone(&options, &intf_row->options);
        smap_replace(&options, OVSREC_INTERFACE_OPTIONS_VNI_LIST,
                    new_vni_list);
        ovsrec_interface_set_options(intf_row, &options);
        smap_destroy(&options);
        free(new_vni_list);

        status_txn = cli_do_config_finish(tunnel_txn);
        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "%% Cannot modify tunnel destination ip."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }
    return CMD_SUCCESS;
}

DEFUN (cli_no_set_vni_list,
        cli_no_set_vni_list_cmd,
        "no vxlan-vni <1-8000>",
        NO_STR
        TUNNEL_VXLAN_VNI_LIST_STR
        TUNNEL_VXLAN_VNI_LIST_NUM_HELP_STR
        TUNNEL_VXLAN_VNI_LIST_HELP_STR)
{
    struct smap options = SMAP_INITIALIZER(&options);
    const struct ovsrec_interface *intf_row = NULL;
    const struct ovsrec_logical_switch *ls_row = NULL;
    struct ovsdb_idl_txn *tunnel_txn = NULL;
    enum ovsdb_idl_txn_status status_txn;
    char *tunnel_name = NULL;
    int64_t ls_tunnel_key = 0;
    char *new_vni_list = NULL;
    const char *cur_vni_list = NULL;

    tunnel_name = (char *)vty->index;
    ls_tunnel_key = (int64_t) atoi(argv[0]);

    intf_row = get_interface_by_name(tunnel_name);
    ls_row = get_logical_switch_by_vni(ls_tunnel_key);

    if(intf_row)
    {
        if(!ls_row)
        {
            vty_out(vty, "%% Can't delete vni from the vni_list as given "
                    "tunnel_key %ld doesn't exist %s", ls_tunnel_key,
                    VTY_NEWLINE);
            return CMD_WARNING;
        }
        tunnel_txn = cli_do_config_start();
        if (tunnel_txn == NULL)
        {
            VLOG_DBG("Transaction creation failed by %s. Function=%s, Line=%d",
                     " cli_do_config_start()", __func__, __LINE__);
            cli_do_config_abort(tunnel_txn);
            return CMD_OVSDB_FAILURE;
        }

        cur_vni_list = smap_get(&intf_row->options, OVSREC_INTERFACE_OPTIONS_VNI_LIST);

        /* Remove given vni from vni_list */
        new_vni_list = remove_vni_to_vni_list(cur_vni_list, argv[0]);

        smap_clone(&options, &intf_row->options);
        if (new_vni_list != '\0')
            smap_replace(&options,
                OVSREC_INTERFACE_OPTIONS_VNI_LIST, new_vni_list);
        else
            smap_remove(&options,
                OVSREC_INTERFACE_OPTIONS_VNI_LIST);

        ovsrec_interface_set_options(intf_row, &options);
        smap_destroy(&options);

        free(new_vni_list);

        status_txn = cli_do_config_finish(tunnel_txn);
        if(status_txn == TXN_SUCCESS || status_txn == TXN_UNCHANGED)
            return CMD_SUCCESS;
    }
    else
    {
        vty_out(vty, "%% Cannot modify tunnel destination ip."
                    "Specified tunnel interface doesn't exist%s", VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }
    return CMD_SUCCESS;
}

DEFUN (cli_set_tunnel_ttl,
       cli_set_tunnel_ttl_cmd,
       "ttl <1-255>",
       TUNNEL_TTL_HELP_STR
       "Set the TTL value\n")
{
    const struct ovsrec_interface *if_row = NULL;

    if_row = get_interface_by_name((char*)vty->index);
    if (!if_row)
    {
        vty_out(vty, "%% Invalid tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return set_intf_option(if_row, OVSREC_INTERFACE_OPTIONS_TTL,
                           argv[0]);
}

DEFUN (cli_no_tunnel_ttl,
       cli_no_tunnel_ttl_cmd,
       "no ttl",
       NO_STR
       TUNNEL_NO_TTL_HELP_STR)
{
    const struct ovsrec_interface *if_row = NULL;

    if_row = get_interface_by_name((char*)vty->index);
    if (!if_row)
    {
        vty_out(vty, "%% Invalid tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    return set_intf_option(if_row, OVSREC_INTERFACE_OPTIONS_TTL,
                           NULL);
}

DEFUN (cli_show_run_intf_tunnel_val,
       cli_show_run_intf_tunnel_val_cmd,
       "show running-config interface tunnel " TUNNEL_INTF_RANGE,
       SHOW_STR
       RUNNING_CONFIG_STR
       INTERFACE_STR
       TUNNEL_STR
       TUNNEL_NUM_HELP_STR)
{
    const struct ovsrec_interface *if_row = NULL;
    char tunnel_name[MAX_TUNNEL_LENGTH] = {0};

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s%s","tunnel", argv[0]);

    if_row = get_interface_by_name(tunnel_name);
    if (!if_row)
    {
        vty_out(vty, "%% Invalid tunnel interface %s%s",
                (char*)vty->index, VTY_NEWLINE);
        return CMD_OVSDB_FAILURE;
    }

    print_tunnel_intf_run_cfg(if_row, idl, NULL /* cbmsg */, vty);

    return CMD_SUCCESS;
}

DEFUN (cli_show_run_intf_tunnel,
       cli_show_run_intf_tunnel_cmd,
       "show running-config interface tunnel",
       SHOW_STR
       RUNNING_CONFIG_STR
       INTERFACE_STR
       TUNNEL_STR)
{
    const struct ovsrec_interface *if_row = NULL;

    OVSREC_INTERFACE_FOR_EACH(if_row, idl) {
        print_tunnel_intf_run_cfg(if_row, idl, NULL /* cbmsg */, vty);
    }

    return CMD_SUCCESS;
}

DEFUN (cli_show_intf_tunnel,
       cli_show_intf_tunnel_cmd,
       "show interface tunnel {brief}",
       SHOW_STR
       INTERFACE_STR
       TUNNEL_STR
       "Show brief info of tunnel interfaces\n")
{
    bool brief = false;
    bool tunnel_only = true;

    if (argv[0] && !strcmp(argv[0], "brief"))
    {
        brief = true;
    }

    // Set to NULL since it is used perform a specific interface lookup
    argv[0] = NULL;

    return cli_show_interface_exec(self, vty, vty_flags, argc, argv, brief,
                                   tunnel_only);
}

DEFUN (cli_show_intf_tunnel_ifname,
       cli_show_intf_tunnel_ifname_cmd,
       "show interface tunnel " TUNNEL_INTF_RANGE " {brief}",
       SHOW_STR
       INTERFACE_STR
       TUNNEL_STR
       TUNNEL_NUM_HELP_STR
       "Show brief info of tunnel interface\n")
{
    bool brief = false;
    bool tunnel_only = true;
    char tunnel_name[MAX_TUNNEL_LENGTH] = {0};

    snprintf(tunnel_name, MAX_TUNNEL_LENGTH, "%s%s","tunnel", argv[0]);
    argv[0] = tunnel_name;

    if (argv[1] && !strcmp(argv[1], "brief"))
    {
        brief = true;
    }

    return cli_show_interface_exec(self, vty, vty_flags, argc, argv, brief,
                                   tunnel_only);
}

/* ovsdb table initialization */
static void
tunnel_ovsdb_init()
{
    ovsdb_idl_add_table(idl, &ovsrec_table_port);
    ovsdb_idl_add_column(idl, &ovsrec_port_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_port_col_interfaces);
    ovsdb_idl_add_column(idl, &ovsrec_port_col_ip4_address);
    ovsdb_idl_add_column(idl, &ovsrec_port_col_ip4_address_secondary);
    ovsdb_idl_add_column(idl, &ovsrec_port_col_vlan_tunnel_keys);

    ovsdb_idl_add_table(idl, &ovsrec_table_logical_switch);
    ovsdb_idl_add_column(idl, &ovsrec_logical_switch_col_tunnel_key);
    ovsdb_idl_add_column(idl, &ovsrec_logical_switch_col_mcast_group_ip);
    ovsdb_idl_add_column(idl, &ovsrec_logical_switch_col_replication_group_ips);
    ovsdb_idl_add_column(idl, &ovsrec_logical_switch_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_logical_switch_col_description);

    ovsdb_idl_add_table(idl, &ovsrec_table_interface);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_type);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_options);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_statistics);

    ovsdb_idl_add_table(idl, &ovsrec_table_vlan);
    ovsdb_idl_add_column(idl, &ovsrec_vlan_col_tunnel_key);
}

/* Initialize ops-switchd cli node. */

void
cli_pre_init(void)
{
    /* ops-switchd doesn't have any context level cli commands.
     * To load ops-switchd cli shared libraries at runtime, this function is required.
     */
    /* Tunnel tables. */
    tunnel_ovsdb_init();
}

void
gre_tunnel_add_clis(void)
{
    install_element(CONFIG_NODE, &cli_create_gre_tunnel_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_set_tunnel_ip_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_no_set_tunnel_ip_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_no_set_tunnel_ip_val_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_set_source_ip_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_no_set_source_ip_val_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_no_set_source_ip_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_set_dest_ip_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_no_set_dest_ip_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_no_set_dest_ip_val_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_set_gre_source_intf_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_no_gre_source_intf_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE,
                    &cli_no_gre_source_intf_val_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_set_tunnel_ttl_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_no_tunnel_ttl_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_intf_mtu_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &no_cli_intf_mtu_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &vtysh_exit_interface_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &vtysh_end_all_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &cli_intf_shutdown_cmd);
    install_element(GRE_TUNNEL_INTERFACE_NODE, &no_cli_intf_shutdown_cmd);
}

/* Install Tunnel related vty commands. */
void
cli_post_init(void)
{
    vtysh_ret_val retval = e_vtysh_error;

    /* Installing global vni commands */
    install_element(CONFIG_NODE, &cli_set_global_vlan_to_vni_mapping_cmd);
    install_element(CONFIG_NODE, &cli_no_set_global_vlan_to_vni_mapping_cmd);

    /* Installing interface vxlan related commands */
    install_element(CONFIG_NODE, &cli_create_tunnel_cmd);
    install_element(CONFIG_NODE, &cli_update_tunnel_cmd);
    install_element(CONFIG_NODE, &cli_delete_tunnel_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_source_intf_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_source_intf_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_source_ip_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_source_ip_val_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_source_ip_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_dest_ip_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_dest_ip_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_dest_ip_val_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_vlan_to_vni_mapping_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_vlan_to_vni_mapping_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_vxlan_udp_port_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_vxlan_udp_port_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_set_vni_list_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_no_set_vni_list_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &vtysh_exit_tunnel_interface_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &vtysh_end_all_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &cli_intf_shutdown_cmd);
    install_element(VXLAN_TUNNEL_INTERFACE_NODE, &no_cli_intf_shutdown_cmd);

    /* Installing vni related commands */
    install_element(CONFIG_NODE, &cli_set_vxlan_tunnel_key_cmd);
    install_element(CONFIG_NODE, &cli_no_set_vxlan_tunnel_key_cmd);
    install_element(VNI_NODE, &cli_set_tunnel_description_cmd);
    install_element(VNI_NODE, &cli_no_set_tunnel_description_cmd);
    install_element(VNI_NODE, &cli_set_vxlan_tunnel_name_cmd);
    install_element(VNI_NODE, &cli_no_set_vxlan_tunnel_name_cmd);
    install_element(VNI_NODE, &cli_set_multicast_group_ip_cmd);
    install_element(VNI_NODE, &cli_no_set_multicast_group_ip_cmd);
    install_element(VNI_NODE, &cli_set_replication_group_ips_cmd);
    install_element(VNI_NODE, &cli_no_set_replication_group_ips_cmd);
    install_element(VNI_NODE, &vtysh_exit_vni_cmd);
    install_element (VNI_NODE, &vtysh_end_all_cmd);

    /* Install show commands */
    install_element(ENABLE_NODE, &cli_show_run_intf_tunnel_cmd);
    install_element(ENABLE_NODE, &cli_show_run_intf_tunnel_val_cmd);
    install_element(ENABLE_NODE, &cli_show_intf_tunnel_cmd);
    install_element(ENABLE_NODE, &cli_show_intf_tunnel_ifname_cmd);

    /* Installing running config sub-context with global config context */
    retval = install_show_run_config_subcontext(e_vtysh_config_context,
                                     e_vtysh_config_context_tunnel,
                                     &vtysh_tunnel_context_clientcallback,
                                     NULL, NULL);
    if(e_vtysh_ok != retval)
    {
        vtysh_ovsdb_config_logmsg(VTYSH_OVSDB_CONFIG_ERR,
                           "config context unable to add vni client callback");
        assert(0);
    }

    /* Installing running config sub-context with global config context */
    retval = install_show_run_config_subcontext(e_vtysh_config_context,
                                     e_vtysh_config_context_tunnel_intf,
                                     &vtysh_tunnel_intf_context_clientcallback,
                                     NULL, NULL);
    if(e_vtysh_ok != retval)
    {
        vtysh_ovsdb_config_logmsg(VTYSH_OVSDB_CONFIG_ERR,
            "config context unable to add tunnel interface client callback");
        assert(0);
    }

    /* Installing GRE related commands */
    gre_tunnel_add_clis();
}
