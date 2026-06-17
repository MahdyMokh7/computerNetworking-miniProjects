#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/wifi-mac-header.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("WifiAxPhase3");

// Periodic simulation progress output
void PrintProgress () {
    std::cout << "Current Simulation Time: " << ns3::Simulator::Now ().GetSeconds () << "s" << std::endl;
    ns3::Simulator::Schedule (ns3::Seconds (1.0), &PrintProgress);
}

std::map<Mac48Address, double> staTotalSinr;
std::map<Mac48Address, uint32_t> staBeaconCount;

// Custom PHY RX trace for SINR and Beacon count
void SinrTrace (std::string context, Ptr<const Packet> packet, uint16_t channelFreqMhz, WifiTxVector txVector, MpduInfo aMpdu, SignalNoiseDbm signalNoise, uint16_t staId)
{
    WifiMacHeader hdr;
    if (packet->PeekHeader (hdr)) {
        // Only read MAC address for Data or Management frames
        if (hdr.IsData() || hdr.IsMgt()) {
            Mac48Address sender = hdr.GetAddr2(); 
            
            if (hdr.IsBeacon ()) {
                staBeaconCount[sender]++;
            } else {
                // Calculate approximate SINR
                double sinr = signalNoise.signal - signalNoise.noise;
                staTotalSinr[sender] += sinr;
            }
        }
    }
}

// Custom MAC RX trace
void RxTraceWithAddressParam(std::string context, Ptr<const Packet> packet) {
    WifiMacHeader hdr;
    if (packet->PeekHeader(hdr)) {
        // Extract MAC address only for Data or Management frames
        if (hdr.IsData() || hdr.IsMgt()) {
            Mac48Address senderMac = hdr.GetAddr2();
            
            std::cout << "[RxTrace] Time: " << Simulator::Now().GetSeconds() << "s"
                      << " | Sender MAC: " << senderMac 
                      << " | Bytes: " << packet->GetSize() << std::endl;
        }
    }
}

int main (int argc, char *argv[])
{
    uint32_t nWifi = 40; 
    double simTime = 10.0; 
    double minDistance = 5.0; 
    double maxDistance = 50.0; 

    CommandLine cmd;
    cmd.AddValue ("nWifi", "Number of wifi STA devices", nWifi);
    cmd.Parse (argc, argv);

    // 1. Create nodes
    NodeContainer wifiStaNodes;
    wifiStaNodes.Create (nWifi);
    NodeContainer wifiApNode;
    wifiApNode.Create (1);

    // 2. Configure PHY layer
    SpectrumWifiPhyHelper spectrumPhy;
    Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel> ();
    Ptr<FriisPropagationLossModel> lossModel = CreateObject<FriisPropagationLossModel> ();
    Ptr<ConstantSpeedPropagationDelayModel> delayModel = CreateObject<ConstantSpeedPropagationDelayModel> ();
    
    spectrumChannel->AddPropagationLossModel (lossModel);
    spectrumChannel->SetPropagationDelayModel (delayModel);
    spectrumPhy.SetChannel (spectrumChannel);
    spectrumPhy.SetErrorRateModel ("ns3::NistErrorRateModel");

    WifiHelper wifi;
    wifi.SetStandard (WIFI_STANDARD_80211ax);
    
    // 3. Configure MAC layer and Scheduler
    WifiMacHelper mac;
    Ssid ssid = Ssid ("ns-3-80211ax-uplink");

    // STA MAC setup (No Multi-User scheduler)
    mac.SetType ("ns3::StaWifiMac",
                 "Ssid", SsidValue (ssid),
                 "ActiveProbing", BooleanValue (false));
    NetDeviceContainer staDevices = wifi.Install (spectrumPhy, mac, wifiStaNodes);

    // AP Scheduler setup (Enable UL OFDMA and BSRP)
    mac.SetMultiUserScheduler ("ns3::RrMultiUserScheduler",
                               "EnableUlOfdma", BooleanValue (true),
                               "EnableBsrp", BooleanValue (true));

    // AP MAC setup
    mac.SetType ("ns3::ApWifiMac",
                 "Ssid", SsidValue (ssid),
                 "EnableBeaconJitter", BooleanValue (false));
    NetDeviceContainer apDevice = wifi.Install (spectrumPhy, mac, wifiApNode);

    // 4. Set Mobility model
    MobilityHelper mobility;
    
    // AP Position (Fixed at center, height 1.5m)
    Ptr<ListPositionAllocator> apPosition = CreateObject<ListPositionAllocator> ();
    apPosition->Add (Vector (0.0, 0.0, 1.5)); 
    mobility.SetPositionAllocator (apPosition);
    mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    mobility.Install (wifiApNode);

    // STAs Position (Random disc around AP)
    mobility.SetPositionAllocator ("ns3::RandomDiscPositionAllocator",
                                   "X", StringValue ("0.0"),
                                   "Y", StringValue ("0.0"),
                                   "Rho", StringValue ("ns3::UniformRandomVariable[Min=" + std::to_string(minDistance) + "|Max=" + std::to_string(maxDistance) + "]"));
    mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    mobility.Install (wifiStaNodes);

    // 5. Install Internet stack and IP addresses
    InternetStackHelper stack;
    stack.Install (wifiApNode);
    stack.Install (wifiStaNodes);

    Ipv4AddressHelper address;
    address.SetBase ("192.168.1.0", "255.255.255.0");
    Ipv4InterfaceContainer staInterfaces = address.Assign (staDevices);
    Ipv4InterfaceContainer apInterface = address.Assign (apDevice);

    // 6. Traffic Generation (Uplink: STA -> AP)
    uint16_t port = 9;

    // AP UDP Sink (Receiver)
    PacketSinkHelper sink ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
    ApplicationContainer serverApp = sink.Install (wifiApNode.Get (0));
    serverApp.Start (Seconds (0.5));
    serverApp.Stop (Seconds (simTime));

    // STA UDP Clients (Transmitters)
    UdpClientHelper client (apInterface.GetAddress (0), port);
    client.SetAttribute ("MaxPackets", UintegerValue (1000000));
    client.SetAttribute ("Interval", TimeValue (MilliSeconds (5))); 
    client.SetAttribute ("PacketSize", UintegerValue (93));         

    ApplicationContainer clientApps;
    
    // Random start time to avoid initial collision
    Ptr<UniformRandomVariable> startTimeRv = CreateObject<UniformRandomVariable> ();
    startTimeRv->SetAttribute ("Min", DoubleValue (1.0));
    startTimeRv->SetAttribute ("Max", DoubleValue (2.0));

    for (uint32_t i = 0; i < wifiStaNodes.GetN (); ++i)
    {
        ApplicationContainer app = client.Install (wifiStaNodes.Get (i));
        double randomStartTime = startTimeRv->GetValue ();
        app.Start (Seconds (randomStartTime)); 
        app.Stop (Seconds (simTime));
        clientApps.Add (app);
    }

    // Schedule progress and setup traces
    Simulator::Schedule (Seconds (1.0), &PrintProgress);
    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> flowmon = flowmonHelper.InstallAll ();
    
    // Connect custom traces
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/MonitorSnifferRx", MakeCallback(&SinrTrace));
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacRx", MakeCallback(&RxTraceWithAddressParam));

    // 7. Run Simulation
    Simulator::Stop (Seconds (simTime + 0.1));
    
    NS_LOG_UNCOND ("Starting Simulation...");
    Simulator::Run ();

    // 8. Output Statistics
    flowmon->CheckForLostPackets ();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmonHelper.GetClassifier ());
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowmon->GetFlowStats ();
    
    double totalThroughput = 0.0;
    double totalDelay = 0.0;
    uint32_t totalTxPackets = 0;
    uint32_t totalRxPackets = 0;
    uint32_t totalLostPackets = 0;
    
    for (auto const &flow : stats) {
        totalTxPackets += flow.second.txPackets;
        totalRxPackets += flow.second.rxPackets;
        totalLostPackets += flow.second.lostPackets;
        
        // Calculate Throughput based on 10 seconds simulation time
        totalThroughput += flow.second.rxBytes * 8.0 / 10.0 / 1e6; 
        
        if (flow.second.rxPackets > 0) {
            totalDelay += flow.second.delaySum.GetSeconds();
        }
    }
    
    std::cout << "\n--- Flow Monitor Statistics ---\n";
    std::cout << "Total Throughput:   " << totalThroughput << " Mbps\n";
    std::cout << "Average Delay:      " << (totalRxPackets > 0 ? (totalDelay / totalRxPackets) : 0) << " Seconds\n";
    std::cout << "Packet Loss Ratio:  " << (totalTxPackets > 0 ? ((double)totalLostPackets / totalTxPackets * 100) : 0) << " %\n";
    
    std::cout << "\n--- Trace Statistics ---\n";
    std::cout << "Nodes tracked for SINR: " << staTotalSinr.size() << "\n";
    std::cout << "Nodes transmitting Beacons: " << staBeaconCount.size() << "\n";

    NS_LOG_UNCOND ("Simulation Finished.");
    
    Simulator::Destroy ();

    return 0;
}