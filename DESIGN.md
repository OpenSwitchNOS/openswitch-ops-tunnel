# High Level design of L2 and L3 VxLAN VTEP (virtual tunnel endpoint)


## Contents

- [Description](#description)
- [Architectural Diagram](#architectural-diagram)
- [Design choices](#design-choices)
- [Unsupported features](#unsupported-features)
- [Key Components](#key-components)
- [OVSDB schema changes](#ovsdb-schema-changes)
- [Feature plugin support for VxLAN](#feature-plugin-support-for-vxlan)
- [Setting VXLAN tunnels](#setting-vxlan-tunnels)
- [VLAN binding to a VXLAN tunnel](#vlan-binding-to-a-vxlan-tunnel)
- [BUM Traffic](#bum-traffic)
- [Configuration CLI](#configuration-cli)
- [Show CLI](#show-cli)
- [Provider API](#provider-api)
- [ASIC Provider support](#asic-provider-support)
- [L3 VxLAN control](#l3-vxlan-control)
- [L3 Inter VNI](#l3-inter-vni)
- [References](#references)

<div id='description'>
## Description

VxLAN creates an overlay network to connect L2 endpoints (Hosts/Servers/VMs)
to other L2 endpoints by extending L2 forwarding domain over an L3 network.
L3 VxLAN is an extension of L2 VxLAN that allows routing of L3 traffic to/from a
VxLAN tunnel. It also supports routing ("tunnel hopping") across tunnels.
The feature supports L2 VxLAN, as described in RFC 7348. It also supports
L3 VxLAN.

The key reason for using L2 & L3 VxLAN is to isolate and segment traffic flows
over an L3 network by limiting forwarding and broadcast domain to network
tunnels. It allows multi-tenancy, limits fault domain, improves security and
classification on a per tenant basis.

The feature provides a user interface in the form of CLI and REST to configure
tunnels and monitor tunnel configuration and state.
The feature supports the Dune family of Broadcom ASICs as well as P4 simulation.

<div id='architectural-diagram'/>
## Architectural diagram

```
<pre>

   +-------------------------- Logical Network --------------------------+
   |                                                                     |
   |     +---------------+      CLI or REST                              |
   |     | L3 Tunnel     |           |                                   |
   |     | Manager       |           |                                   |
   |     | send/recv arp |----> OVS database                             |
   |     | Program ASIC  |           |                                   |
   |     +---------------+           |                                   |
   |      |      ^                 bridge.c                              |
   |      |      |                   |                                   |
   |      |                          V                                   |
   |      |  Logical Switch +---------------------+    Tunnel            |
   |      |   L2 bind       |                     |   Creation           |
   |      |      |          |                     |                      |
   |      |      |       ofproto                netdev                   |
   |      |      |          |                     |                      |
   |      |      |          |                     |                      |
   |      |      |    ofproto provider       netdev provider             |
   |      |      |          |                     |                      |
   |      |      |          |                     |                      |
   |      |      |          +---------------------+                      |
   |      V      |                    |                                  |
   |   Linux   Gateway                V                                  |
   |   kernel  Pkts          SIMULATION/ASIC API                         |
   |                                                                     |
   +---------------------------------------------------------------------+

</pre>
```

<div id='design-choices'/>
## Design choices

* Tunnel destination address
  VxLAN use case for core switch configures source and destination IP address
  per tunnel. While it is possible to figure destination address dynamically,
  the existing code base does not support it and adding this support is hard
  and not required.

* VLAN to VNI mapping
  The code supports per port binding of vlan to vni.
  For campus it is better to provide a global VLAN to VNI mapping and
  but support both modes in the future.

* Routed VxLAN configuration
  The design supports a list of IP prefixes (networks) per VNI.
  The rational is that it is more intuitive than a routed VLAN in a context
  of VxLAN logical network and provides better segmentation if we ever map
  the same VLAN to multiple logical networks.

* Control path software to manage L3
  L3 VxLAN and inter VNI routing requirements are complex. It is unlikely
  control path ARP resolution per VNI can be done by Linux kernel without
  any shim software.

<div id='unsupported-features'/>
## Unsupported features

The following OVN/SDN controller features are not supported at this time:

* L3 Multicast on the overlay network
* Unicast replication in the underlay network
* Any mutlicast feature requiring IGMP snooping
* MP-BGP and EVPN for binding remote MACs to tunnels
* Mix of L2 and L3 VxLAN on the same tunnel
* L2 inter VNI bridging

<div id='key-components'/>
## Key Components

* Set up global VTEP table.
* Create and reconfigure tunnels.
* Bind all ports on one VLAN to the same VNI (logical switch)
* Bind VNI(s) to tunnel
* Develop VXLAN ASIC provider, including ASIC APIs.
* Develop VXLAN simulation provider using P4
* Add CLI support for configuration and show commands
* Add diagnostics and logging
* Setup ASIC filters for packets destined to Gateway MAC
* Develop an L3 VxLAN manager to send/recv ARPs and program ASIC

<div id='ovsdb-schema-changes'/>
## OVSDB schema changes

* Modify interface table to support VXLAN tunnel type.
  Set interface options required by the implementation (e.g. tunnel source IP).
* Add a logical switch (vni) table which will store id, name, description,
  multicast address and a list of zero or more routed IP addresses.
* Modify VLAN table to include an optional link to a logical switch.

<div id='feature-plugin-support-for-vxlan'/>
## Feature plugin support for VxLAN

Tunnel support in OpenSwitch is added back. This includes tunnel types in
in the schema, creation of logical interfaces of tunnel type in ovs-vsctl,
and adding netdev-vport support to netdev ASIC provider as well as netdev
simulation provider.

The bridge (vswitchd) is enhanced to create tunnels and program traffic flows
by binding an access port to a tunnel key domain object.
Tunnel setting is performed by implementing netdev-vport class functions to
create/delete tunnels and by enhancing bundle\_set class function in ofproto
provider to assign/unassign ports to tunnels.
Tunnel packet forwarding requires binding an egress port to a tunnel following
dest_ip next hop resolution. ofproto provider add/del l3\_host\_entry is
enhanced to look up tunnel destination IP and invoke tunnel binding to a new
next hop egress port.

VxLAN enhancements are implemented as a set of feature plugins that extend
bridge, ofproto and netdev functionality while preserving compatibility with
openvswitch current and future versions. These set of platform independent
functions include tunnel configuration, logical switch binding, stats
collections and ASIC programming driven by database transactions.

<div id='setting-vxlan-tunnels'/>
## Setting VXLAN tunnels

A VxLAN tunnel may carry a single tunnel key or may support multiple traffic
flows with different tunnel keys.

Each tunnel interface contains source and destination IP. Destination IP
learning from multicast replies is not supported. Loopback source interface
supports tunnel egress port type being a physical port, LAG, MLAG and ECMP
objects.

OVS implements tunnels via a netdev class, which calls a set of functions in
netdev-vport.c.
The netdev provider has to invoke netdev-vport functions, which triggers a call
to instantiate a tunnel in ASIC (BCM) or simulated ASIC (P4 simulation).

<div id='vlan-binding-to-a-vxlan-tunnel'/>
## VLAN binding to a VXLAN tunnel

In order to tunnel packets over a given tunnel, a tunnel/VNI must be created
and a valid VLAN to VNI mapping must exist. Campus VxLAN solution supports a
global VLAN to VNI binding. But ASIC API binding is done per port.
Hence, VLAN to VNI configuration change and every VLAN state change requires
scanning the port table to bind or unbind each port.
Every port state change requires bind/unbind of the port`s VLAN is bound to a
VNI.

<div id='#bum-traffic'/>
## BUM Traffic

L2 VxLAN has to flood L2 broadcast, unknown unicast and multicast packets to
a multicast group on a per tunnel basis. For example, an ARP request on a VNI
is sent to all tunnels bound to that VNI. One of the remote hosts unicasts an
ARP reply and the tunnel learns the destination MAC from that reply for any
future traffic. The ASIC may also learn destination VTEP IP.

A multicast address is set per VNI and the VNI is bound to a tunnel.
Replication Manager (RM) is implementing multicast on the switch.
VxLAN registers all multicast groups (one per VNI) on a tunnel with RM.
This multicast address is mapped to a replication group ID by the replication
manager (RM) and stored in tunnel cache. Any subsequent update/delete operation
would reference the replication group ID.


<div id='configuration-cli'/>
## Configuration CLI

The following CLI are supported:
A command to create, delete and modify tunnel.
A command to create, delete and modify logical switch.
A VLAN option to bind a VLAN to a VNI.

Please refer to functional guide for details.

<div id='show-cli'/>
## Show CLI

The following CLI are supported:
A command to show a tunnel including counters
A command to show a logical switch including counters

Please refer to functional guide for details.

<div id='provider-api'/>
## Provider API

* Tunnel setup

  Tunnel create/delete/update uses OVS tunnel creation convention.
  A new VxLAN tunnel is created by adding an interface of type VxLAN.
  Similarly, a tunnel is modified and deleted.
  When a tunnel is created, the OVS tunnel port is hashed using the
  destination IP address as a hash key.
  Initially, the tunnel is created in shutdown state.

  The following must happen to complete tunnel bring up:

  (1) Next Hop lookup for destination IP. On match, call add\_l3\_host\_entry.

  (2) On failure to find next hop, invoke longest IP prefix match.

  (3) On failure wait and repeat 2. On success, execute an arp on neighbor
     interface found in step 2. Wait and repeat step 1.

* Tunnel reconfiguration

  Tunneled packets are sent over next hop egress port. ASIC implementation may
  allow dynamic L3 route lookup on destination IP find egress port.
  Other implementation require specific next hop binding to tunnel on any next
  hop setup or next hop change. A next hop change results is a call to
  l3\_host\_entry in ofproto provider. The function invokes a tunnel hash lookup
  on the input IP address. A match indicates that a new next hop egress port
  has to be assigned to that tunnel.

* Access port binding

  ofproto\_bundle\_settings structure is expanded to include vlan to tunnel-key
  binding smap. port\_configure function in bridge.c has to set it up before
  invoking ofproto\_bundle\_register.
  ofproto can bind the port to a tunnel key object. Logical switch binding
  to a tunnel key K is only possible if a tunnel with key=K exists.

<div id='asic-provider-support'/>
## ASIC Provider support

* Create Tunnels

  The VTEP daemon creates an OVSDB tunnel by adding a tunnel to interface table.
  The bridge invokes netdev to add a tunnel whenever a tunnel interface is added.
  The netdev provider instructs the ASIC to create a tunnel. Similarly, tunnel
  deletion and modification will result in netdev provider deleting a tunnel by
  invoking the ASIC API to do so. Modifying a tunnel is done by deleting existing
  tunnel and creating a new one with alternate tunnel parameters.

* Assigning ports to VXLAN tunnels

  The provider invokes an ASIC API to bind the local port to a VXLAN tunnel
  key. The bridge scans the port table for changes. When a local port is bound to
  a VNI, ofproto provider function is called. The inverse happens on VNI
  unbound.

<div id='l3-vxlan-control'>
## L3 VxLAN control plane processing

  VxLAN control plane management is in charge of terminating L3 packets and
  routing them over a VxLAN tunnel and vice versa. It also does routing
  between tunnels. Key components include a tunnel daemon, bridge feature
  plugin, a CPU trap (filter) to intercept VxLAN packets sent to Gateway
  as well as a CPU interface to send packets from a CPU into a tunnel via
  (port, vlan, vni) binding.

  A tunnel daemon reads logical switch table and creates a route lookup map
  for all routed VxLAN interfaces. A route entry stores corresponding VNI, VLAN
  and a list of tunnels bound to that VNI.

  A tunnel daemon listens to ingress packets on a VNI routed interface. It
  resolves destination MAC from destination IP by either a cache hit or by
  sending an ARP request on all the tunnels bound to that VNI.
  ARP response will arrive on a CPU trap. A new ARP entry in local cache
  is created or updated and a table entry in OVSDB host table is added.
  Bridge feature plugin programs ARP entry into ASIC L3 VxLAN with
  destination being a tunnel object. It also programs an ARP entry from
  the tunnel into the L3 host.

  A tunnel daemon listens to ARP requests arriving via ASIC (KM) traps to the
  CPU interface. These packets are L2 packets that need to be routed out on
  an L3. The daemon generates an ARP reply, does VxLAN header encap and sends
  the reply out on the VTEP source (network egress) interface. It then updates
  arp cache and updates OVSDB (see last paragraph).

### VxLAN control interfaces

  The daemon sends VxLAN packets on a KNET transmit interface. The daemon
  receives packets by registering an RCPU filter with the ASIC which will
  forward any ARP packets destined to gateway MAC to the CPU along with outer
  L2/L3 header used to identify the VNI and tunnel.

  The daemon also intercepts packet arriving on VxLAN connected routes by
  reading incoming packets from L3 tap devices.

### Routing between L3 and a VxLAN tunnel

  VxLAN daemon would listen to VNI routed interface updates. Once a new route
  is added, the daemon creates a tun interface and bind the new route to that
  interface. The daemon intercepts incoming L3 packets, identifies the outgoing
  VNI, map VNI to VLAN, resolve destination L2 MAC by sending an ARP over a
  VxLAN tunnel via CPU egress interface, update ARP cache and host table on
  ARP reply. That triggers an ARP entry update in ASIC.
  The daemon also listens to ARP requests to Gateway (CPU) and follow similar
  process to program cache and ASIC.

### Routing between VxLAN tunnels (inter VNI)

  The simplest way to implement inter VNI routing is to let the ASIC do the
  forwarding based on destination IP address, VNI and tunnel destination.
  A more complex topology, like multiple tunnels per VNI, may require CPU
  termination and initiation, similar to L3 VxLAN, discussed earlier, with
  the exception that ARP request/reply has to happen on ingress VNI as well
  as egress VNI.

### ARP request/response VNI management

  VxLAN daemon shims between L2 packets over VxLAN and routed IP traffic. It
  also routes between VNIs. In order to provide that functionality the VxLAN
  daemon has to intercept L3 packets, initiate an ARP on a VxLAN tunnel, wait
  on an ARP response and then update cache and update OVSDB. It also has to
  terminate ARPs destined to the CPU (inner MAC is Gateway MAC) by sending an
  ARP reply to VxLAN as well as update ARP cache and update OVSDB.


<div id='l3-inter-vni'>
## L3 Inter VNI

  L2 VxLAN termination is similar to L3 VxLAN. The difference is that an ARP
  request arriving on VNI X should trigger sending an ARP request on VNI Y.
  Once ARP is resolved, ASIC is updated by following L3 VxLAN process.

<div id='references'/>
## References

* [openswitch](http://www.openswitch.net/)
* [OpenvSwitch](http://www.openvswitch.org/)
