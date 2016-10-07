# Feature Test Cases

## Contents

- [Objective](#objective)
- [Requirements](#requirements)
- [Setup](#setup)
	- [Topology](#topology)
	-  [Test setup](#testsetup)
- [Test Cases](#testcases)

## Objective

To verify that a virtual point to point tunnel is created over an IP network and ensure that packets are transmitted through the GRE tunnel created between 2 endpoints.

### Requirements

The requirements for this test case are:
	- Switch1 and Switch2
	- Host1 and Host2
	- Virtual tunnel between Switch1 and Switch2

### Setup

Connect Host1 to Switch1 and Host2 to Switch2
Connect interface 1 to eth1 on Switch1.
Connect interface 2 to eth1 on Switch2.
GRE tunnel is created between switch1 and switch2

#### Topology Diagram

```ditaa
+----+		+----+	Tunnel			+----+		  +----+
|hs1 +------+sw1 | <=============>  |sw2 +--------+ hs2|
+----+		+----+					+----+		  +----+
```

#### Test Setup

1. On Host1, assign eth1 with IP 1.1.1.2/24 and connect Host1 to Switch1 on interface 1.
2. On Host2, assign eth1 with IP 1.2.1.2/24 and connect Host2 to Switch2 on interface 1.
3. Create a tunnel between Switch1 and Switch2
		```interface tunnel 10 mode gre ipv4```
		```ip address 192.1.1.1/24```
		```source ip 1.1.1.1```
		```destination ip 1.2.1.1```
		```ttl <0-255>```
4. Create a static route
		```ip route 190.1.10.0/24 tunnel10```

##  GRE_tunnel_creation

### Objective

To verify tunnel creation

### Priority

 3- High

### Description

This test verifies that a tunnel is created between Switch1 and Switch2.

### Test Result Criteria

#### Test Pass Criteria

Test passes if the statistics shows that packet was routed using the tunnel or if Host2 can be pinged from Host1

#### Test Fail Criteria

Test fails if statistics show that the packet is not routed using the tunnel or if Host2 cannot be pinged from Host1

##  GRE_tunnel_deletion

### Delete non existing tunnel

### Objective

To verify tunnel deletion

### Priority

 3- High

### Description

This test case verifies that a non existent tunnel cannot be deleted

### Test Result Criteria

#### Test Pass Criteria

The test passes if a non existent tunnel cannot be deleted

#### Test Fail Criteria

The test fails if the non existent tunnel can be deleted

### Delete existing tunnel

### Objective

To verify tunnel deletion

### Priority

 3- High

### Description

This test verifies that a tunnel between Switch1 and Switch2 is deleted

### Test Result Criteria

#### Test Pass Criteria

This test passes if the tunnel is deleted and packets are not not routed through the tunnel

#### Test Fail Criteria

The test fails if the statistics show that packet was not routed through the tunnel

##  GRE_tunnel_update

### Objective

To verify tunnel modification

### Priority

 3- High

### Description

Modify tunnel parameters as below
```
interface tunnel 10
ip mtu 1200
```
### Test Result Criteria

#### Test Pass Criteria

This test passes if the MTU is modified to the new value

#### Test Fail Criteria

This test fails if the MTU is not modified to the new value

##  GRE_test_packet_encapsulation_decapsulation

### test_packet_encapsulation

### Objective

To verify if GRE header and new header is added to the original packet

### Priority

 3- High

### Description

Have the topology as shown in topology diagram. Ping Host2 from Host1. Verify that the GRE header and additional header is added on Switch1

### Test Result Criteria

#### Test Pass Criteria

The test passes if GRE header and IPv4 header is added

#### Test Fail Criteria

Test fails if the additional headers are not added

### test_packet_decapsulation

### Objective

To verify that the GRE header and IPv4 header is stripped

### Priority

 3- High

### Description

Have the topology as shown in topology diagram. Ping Host2 from Host1. Verify that the GRE header and additional header is removed as seen on Switch2 egress object

### Test Result Criteria

#### Test Pass Criteria

This test passes if GRE header and additional headers are removed

#### Test Fail Criteria

This test fails if GRE header and additional headers are not removed

##  GRE_CLI_verification

### Objective

To verify that OVSDB is updated with the required configuration when configured through the CLI's

### test_show_running_config_interface

#### Objective

To verify that    ```show running-config interface tunnel <number>``` displays the tunnel configuration

### Priority

 3- High

### Description

Configure a GRE tunnel as below.
```
interface tunnel 10 mode gre ipv4
ip address 172.169.1.1/24
source ip 30.1.1.1/24
destination ip 20.1.1.1/24
mtu 1500
ttl 150
```
Then verify that the show running-config interface tunnel 10 displays tunnel configuration as expected

### Test Result Criteria

#### Test Pass Criteria

This test passes if the configuration matches the expected output.

#### Test Fail Criteria

This fails if the configuration does not match the expected output.

### test_show_interface_filter_on_name

#### Objective

This test verifies that ```show interface tunnel <number>``` displays interface information for gre interface.

#### Priority

 3- High

#### Description

Configure a GRE tunnel as below.
```
interface tunnel 10 mode gre ipv4
ip address 172.169.1.1/24
source ip 30.1.1.1/24
destination ip 20.1.1.1/24
mtu 1500
ttl 150
```
Then verify that the ```show interface tunnel 10``` displays information for gre interface as expected.

#### Test Result Criteria

#### Test Pass Criteria

This test passes if the output of show command matches with the expected GRE interface configuration

#### Test Fail Criteria

This test fails if the output of show command does not matche with the expected GRE interface configuration


##  GRE_test_max_number_of_tunnels

### test_max_number_of_tunnels

### Objective
To verify that max(127) tunnels can be created

### Priority

 3- High

### Description

Create topolgy as shown in Topogy section. The maximum number of tunnels for GRE is 127. Create 127 tunnels between Switch1 and Switch2.

### Test Result Criteria

#### Test Pass Criteria

This test passes if the creation of maximum number of tunnels is allowed

#### Test Fail Criteria

This test fails if we cannot create 127 GRE tunnels

### test_max+1_number_of_tunnels

### Objective

To verify that we cannot create more than max(127) GRE tunnels

### Priority

 3- High

### Description

Create topolgy as shown in Topology section. The maximum number of tunnels for GRE is 127. Create maximum + 1  tunnels between Switch1 and Switch2.

### Test Result Criteria

#### Test Pass Criteria

This test passes if max+1 tunnel cannot be created

#### Test Fail Criteria

This test fails if max+1 tunnel can be created

##  GRE_test_IPv6_source_destination

### Objective


### Priority

 3- High

### Description


### Test Result Criteria

#### Test Pass Criteria

#### Test Fail Criteria


##  GRE_test_IPv6_in_IPv4_static_route

### Objective
This test verifies that ipv6 packets are be encapsulated in gre ipv4

### Priority
 3- High

### Description

Create topology as shown in Topology section. ipv6 packets should be encapsulated in gre ipv4.

### Test Result Criteria

#### Test Pass Criteria

This test passes if the IPv6 packet is encapsulated within IPv4 which can be verified if the ping is successful or can be verified using counters

#### Test Fail Criteria

This test fails if the ping fails from Host1 to Host2

##  GRE_test_IPv4_in_IPv4_static_route


### Objective

This test verifies that ipv4 packets are be encapsulated in gre ipv4

### Priority

 3- High

### Description

Create topology as shown in Topology section. ipv4 packets should be encapsulated in gre ipv4.

### Test Result Criteria

#### Test Pass Criteria

This test passes if ping is successful between Host1 and Host2. It can also be verified by checking counters to see if the packet was recieved at Switch2

#### Test Fail Criteria

This test fails if the ping fails from Host1 to Host2

##  GRE_test_jumbo_frames

### Objective

To ensure that Jumbo frames are dropped on the switch

### Priority

 3- High

### Description

Create topology as shown above. Configure MTU such that it is a jumbo frame
```
interface tunnel 10 mode gre ipv4
ip address 172.169.1.1/24
source interface Loopback1 OR
tunnel source 30.1.1.1/24
tunnel destination 20.1.1.1/24
mtu 10000
ttl 150
```

### Test Result Criteria

#### Test Pass Criteria

This test passes if the packet is dropped on the switch

#### Test Fail Criteria

This test fails if the packet is not dropped on the switch

##  GRE_test_TTL_value

### test_ttl_1_packet_for_switch

### Objective

To ensure that a packet destined to a switch with TTL1 is received by Switch2

### Priority

 3- High

### Description

TTL is set to 1 and packet is destined to go to Switch2

Create topology as shown above. Set TTL to 1
```
interface tunnel 10 mode gre ipv4
ip address 172.169.1.1/24
source ip 30.1.1.1/24
destination ip 20.1.1.1/24
mtu 1500
ttl 1
```
When encapsulated packet with TTL=1 reached Switch2, TTL becomes 0. Since packet is destined for Switch2, packet is recieved.

### Test Result Criteria

#### Test Pass Criteria

This test passes if the packet is received by Switch2

#### Test Fail Criteria

This test fails if the packet is not received by Switch2

### test_ttl_1_packet_not_for_switch

### Objective

To ensure that a packet not destined to a switch with TTL1 has to be dropped

### Priority

 3- High

### Description

Create topology as shown. Set TTL to 1.


```ditaa

+----+		+----+	     Tunnel     +----+		  +----+
|hs1 +------+sw1 | <=============>  |sw2 +--------+ hs2|
+----+		+--+-+                  +--+-+        +----+
               |       +----+          |
               |-------| s3 |----------|
                       +----+
```

```
interface tunnel 10 mode gre ipv4
ip address 172.169.1.1/24
source interface Loopback1 OR
tunnel source 30.1.1.1/24
tunnel destination 20.1.1.1/24
mtu 10000
ttl 1
```

Encapsulated packet is destined for Swich2. Since TTL = 1, when packet reached Switch3 TTL=0 and the packet is dropped on Switch3.

### Test Result Criteria

#### Test Pass Criteria

This test passes if the packet not destined to the Switch3 is dropped

#### Test Fail Criteria

This test fails if the packet not destined to the switch and has TTL 1 is not dropped

### test_ttl_max

### Objective

To ensure that  a packet not destined for Switch2 and TTL set to max should be sent out of the switch

### Priority

 3- High

### Description
Create topology as shown. Set TTL to 1.


```ditaa

+----+		+----+	     Tunnel     +----+		  +----+
|hs1 +------+sw1 | <=============>  |sw2 +--------+ hs2|
+----+		+--+-+                  +--+-+        +----+
               |       +----+          |
               |-------| s3 |----------|
                       +----+
```

```
interface tunnel 10 mode gre ipv4
ip address 172.169.1.1/24
source interface Loopback1 OR
tunnel source 30.1.1.1/24
tunnel destination 20.1.1.1/24
mtu 1500
ttl <max>
```

TTL is set to max. The packet is destined for Switch2. So when packet reaches Switch3 from Switch1 TTL is reduced by 1 and forwarded to Switch2. Since packet is not destined for Switch2, it is sent out of the tunnel.

### Test Result Criteria

#### Test Pass Criteria

This test passes if packet is sent out of the tunnel

#### Test Fail Criteria

This test fails if the packet is not sent out of the tunnel

##  GRE_test_trobleshooting_commands

### test_diagnostics

### Objective


### Priority

 3- High

### Description


### Test Result Criteria

#### Test Pass Criteria

#### Test Fail Criteria

### test_logging

### Objective

### Priority

 3- High

### Description


### Test Result Criteria

#### Test Pass Criteria

#### Test Fail Criteria


### test_show_tech

### Objective


### Priority

 3- High

### Description


### Test Result Criteria

#### Test Pass Criteria

#### Test Fail Criteria
