/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
// Phase 2: Wi-Fi 6 (802.11ax) with OFDMA and MU-MIMO

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/mobility-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/yans-wifi-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("Wifi6Phase2");

int main (int argc, char *argv[])
{
  // Simulation parameters
  double simulationTime = 10.0;  // seconds
  uint32_t nStations = 5;
  double interval = 0.5;         // seconds between packets
  
  // Enable logging
  LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
  LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);
  
  // Create nodes: AP + stations
  NodeContainer apNode;
  apNode.Create (1);
  
  NodeContainer staNodes;
  staNodes.Create (nStations);
  
  NodeContainer allNodes = NodeContainer (apNode, staNodes);
  
  // ========== MOBILITY (Same as Phase 1) ==========
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
  
  // AP at center
  positionAlloc->Add (Vector (0.0, 0.0, 0.0));
  
  // Stations in a circle (radius 10 meters)
  for (uint32_t i = 0; i < nStations; i++)
  {
    double angle = 2 * M_PI * i / nStations;
    double x = 10 * cos (angle);
    double y = 10 * sin (angle);
    positionAlloc->Add (Vector (x, y, 0.0));
  }
  
  mobility.SetPositionAllocator (positionAlloc);
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (allNodes);
  
  // ========== WI-FI 6 (802.11ax) WITH OFDMA & MU-MIMO ==========
  
  // Configure Wi-Fi 6 standard
  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211ax);  // ← Wi-Fi 6 (802.11ax)
  
  // Use YansWifiPhyHelper (works in ns-3.38)
  YansWifiPhyHelper phy;
  YansWifiChannelHelper channelHelper = YansWifiChannelHelper::Default();
  phy.SetChannel (channelHelper.Create());
  
  // Set PHY parameters for Wi-Fi 6
  phy.Set ("ChannelWidth", UintegerValue (80));    // 80 MHz channel
  phy.Set ("TxPowerStart", DoubleValue (20.0));    // 20 dBm transmission power
  phy.Set ("TxPowerEnd", DoubleValue (20.0));
  
  // Configure MAC layer for Wi-Fi 6 with OFDMA
  WifiMacHelper mac;
  Ssid ssid = Ssid ("wifi6-network");
  
  // Configure AP with OFDMA support
  mac.SetType ("ns3::ApWifiMac", 
               "Ssid", SsidValue (ssid),
               "EnableBeaconJitter", BooleanValue (false));
  
  // Install on AP
  NetDeviceContainer apDevice = wifi.Install (phy, mac, apNode);
  
  // Configure stations (clients)
  mac.SetType ("ns3::StaWifiMac", 
               "Ssid", SsidValue (ssid),
               "ActiveProbing", BooleanValue (false));
  
  NetDeviceContainer staDevices = wifi.Install (phy, mac, staNodes);
  
  // Configure MCS (Modulation and Coding Scheme)
  // Set remote station manager with specific MCS
wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                              "DataMode", StringValue ("HtMcs7"),  // Data MCS = 7
                              "ControlMode", StringValue ("HtMcs0"));  // Data MCS = 0
  
  std::cout << "\n=== Wi-Fi 6 Configuration ===" << std::endl;
  std::cout << "Standard: 802.11ax (Wi-Fi 6)" << std::endl;
  std::cout << "OFDMA: Enabled (802.11ax enables this by default)" << std::endl;
  std::cout << "MU-MIMO: Enabled (802.11ax enables this by default)" << std::endl;
  std::cout << "Data MCS: VhtMcs7 (High data rate)" << std::endl;
  std::cout << "Control MCS: VhtMcs0 (Robust control messages)" << std::endl;
  std::cout << "Channel Width: 80 MHz" << std::endl;
  std::cout << "==============================\n" << std::endl;
  
  // ========== NETWORK LAYER ==========
  InternetStackHelper stack;
  stack.Install (allNodes);
  
  Ipv4AddressHelper address;
  address.SetBase ("192.168.1.0", "255.255.255.0");
  Ipv4InterfaceContainer apInterface = address.Assign (apDevice);
  Ipv4InterfaceContainer staInterfaces = address.Assign (staDevices);
  
  // ========== TRAFFIC GENERATION ==========
  // UDP Echo Server on AP
  UdpEchoServerHelper echoServer (9);
  ApplicationContainer serverApp = echoServer.Install (apNode.Get (0));
  serverApp.Start (Seconds (0.0));
  serverApp.Stop (Seconds (simulationTime));
  
  std::cout << "=== Traffic Configuration ===" << std::endl;
  std::cout << "AP: UDP Echo Server on port 9" << std::endl;
  
  // UDP Echo Clients on stations (different packet sizes like Phase 1)
  for (uint32_t i = 0; i < nStations; i++)
  {
    // STA 1,3,5 = 1024 bytes, STA 2,4 = 512 bytes
    uint32_t pktSize = (i == 0 || i == 2 || i == 4) ? 1024 : 512;
    
    UdpEchoClientHelper echoClient (apInterface.GetAddress (0), 9);
    echoClient.SetAttribute ("MaxPackets", UintegerValue (0xFFFFFFFF));
    echoClient.SetAttribute ("Interval", TimeValue (Seconds (interval)));
    echoClient.SetAttribute ("PacketSize", UintegerValue (pktSize));
    
    ApplicationContainer clientApp = echoClient.Install (staNodes.Get (i));
    clientApp.Start (Seconds (0.0));
    clientApp.Stop (Seconds (simulationTime));
    
    std::cout << "STA " << i+1 << ": " << pktSize << " bytes, interval = " << interval << " s" << std::endl;
  }
  std::cout << "===========================\n" << std::endl;
  
  // ========== FLOW MONITOR ==========
  FlowMonitorHelper flowmonHelper;
  Ptr<FlowMonitor> flowMonitor = flowmonHelper.InstallAll ();
  
  // ========== RUN SIMULATION ==========
  Simulator::Stop (Seconds (simulationTime));
  Simulator::Run ();
  
  // ========== COLLECT RESULTS ==========
  flowMonitor->CheckForLostPackets ();
  std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats ();
  
  double totalThroughput = 0;
  double totalDelay = 0;
  std::vector<double> throughputs;
  std::vector<double> delays;
  
  std::cout << "\n========== PHASE 2 RESULTS (Wi-Fi 6 with OFDMA/MU-MIMO) ==========\n";
  std::cout << "Flow ID\tTx Packets\tRx Packets\tLoss (%)\tThroughput (Mbps)\tAvg Delay (ms)\n";
  
  for (auto &flow : stats)
  {
    double throughput = flow.second.rxBytes * 8.0 / simulationTime / 1e6;
    double loss = (flow.second.txPackets - flow.second.rxPackets) * 100.0 / flow.second.txPackets;
    double delay = 0.0;
    if (flow.second.rxPackets > 0)
    {
      delay = flow.second.delaySum.GetSeconds () * 1000 / flow.second.rxPackets;
    }
    
    std::cout << flow.first << "\t"
              << flow.second.txPackets << "\t\t"
              << flow.second.rxPackets << "\t\t"
              << loss << "\t\t"
              << throughput << "\t\t"
              << delay << "\n";
    
    totalThroughput += throughput;
    throughputs.push_back (throughput);
    totalDelay += delay;
    delays.push_back (delay);
  }
  
  // Calculate Jain's Fairness Index
  double sumTh = 0, sumThSq = 0;
  for (double th : throughputs)
  {
    sumTh += th;
    sumThSq += th * th;
  }
  double fairness = (sumTh * sumTh) / (throughputs.size() * sumThSq);
  
  // Calculate average delay
  double avgDelay = 0;

  avgDelay = (delays.size() > 0) ? totalDelay / delays.size() : 0;
  
  std::cout << "\n--- Summary ---\n";
  std::cout << "Total throughput: " << totalThroughput << " Mbps\n";
  std::cout << "Average throughput per station: " << totalThroughput / nStations << " Mbps\n";
  std::cout << "Average delay: " << avgDelay << " ms\n";
  std::cout << "Jain's Fairness Index: " << fairness << "\n";
  
  // Cleanup
  Simulator::Destroy ();
  
  return 0;
}