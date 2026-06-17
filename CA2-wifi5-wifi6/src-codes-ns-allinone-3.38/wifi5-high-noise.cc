/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
// Star Topology

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/yans-wifi-helper.h" 
#include "ns3/mobility-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("Wifi5Baseline");

int main (int argc, char *argv[])
{
  // Simulation parameters
  double simulationTime = 10.0;  // seconds
  uint32_t nStations = 5;
  double packetSize = 1024;      // bytes
  double interval = 0.5;         // seconds between packets
  
  // Enable logging for UDP Echo (to see connectivity)
  LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
  LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);
  
  // Create nodes: AP + stations
  NodeContainer apNode;
  apNode.Create (1);
  
  NodeContainer staNodes;
  staNodes.Create (nStations);
  
  // Combine all nodes
  NodeContainer allNodes = NodeContainer (apNode, staNodes);
  
  // Configure mobility: fixed positions (Star topology)
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
  
  // AP at center (0,0,0)
  positionAlloc->Add (Vector (0.0, 0.0, 0.0));
  
  // Stations placed in a circle of radius 10 meters
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
  
  // Configure Wi-Fi 5 (802.11ac)
  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211ac);
  
  WifiMacHelper mac;
  YansWifiPhyHelper phy;
  
  YansWifiChannelHelper channelHelper = YansWifiChannelHelper::Default();
  phy.SetChannel (channelHelper.Create());
  phy.Set ("ChannelWidth", UintegerValue (80));  // 80 MHz for 802.11ac
  phy.Set ("TxPowerStart", DoubleValue (10.0));   // 10 dBm (HIGH NOISE)
  phy.Set ("TxPowerEnd", DoubleValue (10.0));
  
  // Configure SSID
  Ssid ssid = Ssid ("wifi5-network");
  
  // Configure AP
  mac.SetType ("ns3::ApWifiMac", "Ssid", SsidValue (ssid));
  NetDeviceContainer apDevice = wifi.Install (phy, mac, apNode);
  
  // Configure stations
  mac.SetType ("ns3::StaWifiMac", "Ssid", SsidValue (ssid));
  NetDeviceContainer staDevices = wifi.Install (phy, mac, staNodes);
  
  NetDeviceContainer allDevices = NetDeviceContainer (apDevice, staDevices);
  
  // Install Internet stack
  InternetStackHelper stack;
  stack.Install (allNodes);
  
  // Assign IP addresses
  Ipv4AddressHelper address;
  address.SetBase ("192.168.1.0", "255.255.255.0");
  Ipv4InterfaceContainer apInterface = address.Assign (apDevice);
  Ipv4InterfaceContainer staInterfaces = address.Assign (staDevices);
  
  // Install UDP Echo Server on AP
  UdpEchoServerHelper echoServer (9);
  ApplicationContainer serverApp = echoServer.Install (apNode.Get (0));
  serverApp.Start (Seconds (0.0));
  serverApp.Stop (Seconds (simulationTime));
  
  std::cout << "\n=== Traffic Configuration ===" << std::endl;
  std::cout << "AP: UDP Echo Server on port 9" << std::endl;
  
  // Install UDP Echo Clients on each station with DIFFERENT packet sizes
  // STA 1, 3, 5 → 1024 bytes, STA 2, 4 → 512 bytes
  for (uint32_t i = 0; i < nStations; i++)
  {
    // Determine packet size based on station index
    uint32_t pktSize;
    if (i == 0 || i == 2 || i == 4)  // STA 1, 3, 5
    {
      pktSize = 1024;
    }
    else  // STA 2, 4
    {
      pktSize = 512;
    }
    
    UdpEchoClientHelper echoClient (apInterface.GetAddress (0), 9);
    echoClient.SetAttribute ("MaxPackets", UintegerValue (0xFFFFFFFF));
    echoClient.SetAttribute ("Interval", TimeValue (Seconds (interval)));  // 0.5 seconds
    echoClient.SetAttribute ("PacketSize", UintegerValue (pktSize));
    
    ApplicationContainer clientApp = echoClient.Install (staNodes.Get (i));
    clientApp.Start (Seconds (0.0));
    clientApp.Stop (Seconds (simulationTime));
    
    std::cout << "STA " << i+1 << ": UDP Echo Client, packet size = " << pktSize << " bytes, interval = " << interval << " s" << std::endl;
  }
  std::cout << "===========================\n" << std::endl;  
  
  // Install FlowMonitor to collect metrics
  FlowMonitorHelper flowmonHelper;
  Ptr<FlowMonitor> flowMonitor = flowmonHelper.InstallAll ();
  
  // Run simulation
  Simulator::Stop (Seconds (simulationTime));
  Simulator::Run ();
  
  // Collect and print metrics
  flowMonitor->CheckForLostPackets ();
  std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats ();
  
  double totalThroughput = 0;
  double totalDelay = 0;
  std::vector<double> throughputs;
  std::vector<double> delays;
  
  std::cout << "\n========== PHASE 1 RESULTS (Wi-Fi 5 Baseline) ==========\n";
  std::cout << "Flow ID\tTx Packets\tRx Packets\tLoss (%)\tThroughput (Mbps)\tAvg Delay (ms)\n";
  
  for (auto &flow : stats)
  {
    double throughput = flow.second.rxBytes * 8.0 / simulationTime / 1e6;
    double loss = (flow.second.txPackets - flow.second.rxPackets) * 100.0 / flow.second.txPackets;
    double delay = flow.second.delaySum.GetSeconds () * 1000 / flow.second.rxPackets;
    
    std::cout << flow.first << "\t"
              << flow.second.txPackets << "\t\t"
              << flow.second.rxPackets << "\t\t"
              << loss << "\t\t"
              << throughput << "\t\t"
              << delay << "\n";
    
    totalThroughput += throughput;
    throughputs.push_back (throughput);
    totalDelay += delay;
    delays.push_back(delay);
  }
  
  // Calculate Jain's Fairness Index
  double sumTh = 0, sumThSq = 0;
  for (double th : throughputs)
  {
    sumTh += th;
    sumThSq += th * th;
  }
  double fairness = (sumTh * sumTh) / (throughputs.size() * sumThSq);
  
  std::cout << "\n--- Summary ---\n";
  std::cout << "Total throughput: " << totalThroughput << " Mbps\n";
  std::cout << "Average throughput per station: " << totalThroughput / nStations << " Mbps\n";
  std::cout << "Average Delay per station: " << totalDelay / nStations << " ms\n";
  std::cout << "Jain's Fairness Index: " << fairness << "\n";
  
  // Cleanup
  Simulator::Destroy ();
  
  return 0;
}
