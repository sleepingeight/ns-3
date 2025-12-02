#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include <fstream>
#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("OgSim2");

// Global file streams for cwnd tracing
std::ofstream cwndStream1, cwndStream2, cwndStream3;

// Cwnd trace callbacks for flows 1, 2, 3
static void CwndTracer1(uint32_t oldval, uint32_t newval)
{
    cwndStream1 << Simulator::Now().GetSeconds() << "," << newval << std::endl;
}

static void CwndTracer2(uint32_t oldval, uint32_t newval)
{
    cwndStream2 << Simulator::Now().GetSeconds() << "," << newval << std::endl;
}

static void CwndTracer3(uint32_t oldval, uint32_t newval)
{
    cwndStream3 << Simulator::Now().GetSeconds() << "," << newval << std::endl;
}

void RunSimulation(std::string tcpVariant, uint32_t simulationTime, bool verbose, std::string outputDir)
{
    // Configure TCP variant
    if (tcpVariant == "LinuxReno") {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpLinuxReno"));
    } else if (tcpVariant == "Fast") {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpFast"));
    } else {
        NS_ABORT_MSG("Unknown TCP variant: " << tcpVariant);
    }
    
    // TCP parameters (optimized for high-delay environment)
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1400));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(8000000));  // 8MB
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(8000000));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    
    // Create nodes
    Ptr<Node> server = CreateObject<Node>();
    
    NodeContainer routers;
    routers.Create(5);
    
    NodeContainer receivers;
    receivers.Create(15);  // 5 routers × 3 receivers each
    
    // Install Internet stack
    InternetStackHelper stack;
    stack.Install(server);
    stack.Install(routers);
    stack.Install(receivers);
    
    // Point-to-point helper
    PointToPointHelper p2p;
    
    // Server → Router links (6Mbps, 100ms - creates congestion for clear TCP behavior)
    // Reduced from 10Mbps to 6Mbps to create more congestion (45 flows competing)
    // This will trigger more packet loss → show Reno's sawtooth pattern clearly
    p2p.SetDeviceAttribute("DataRate", StringValue("6Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("100ms"));
    
    std::vector<NetDeviceContainer> devServerRouter(5);
    for (uint32_t i = 0; i < 5; i++) {
        devServerRouter[i] = p2p.Install(server, routers.Get(i));
    }
    
    // Router → Receiver links (heterogeneous)
    std::vector<NetDeviceContainer> devRouterReceiver(15);
    
    for (uint32_t router_idx = 0; router_idx < 5; router_idx++) {
        uint32_t receiver_base = router_idx * 3;
        
        // Receiver 0: 1Mbps/50ms
        p2p.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
        p2p.SetChannelAttribute("Delay", StringValue("50ms"));
        p2p.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("50p"));
        devRouterReceiver[receiver_base + 0] = p2p.Install(routers.Get(router_idx), 
                                                            receivers.Get(receiver_base + 0));
        
        // Receiver 1: 2Mbps/25ms
        p2p.SetDeviceAttribute("DataRate", StringValue("2Mbps"));
        p2p.SetChannelAttribute("Delay", StringValue("25ms"));
        p2p.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("50p"));
        devRouterReceiver[receiver_base + 1] = p2p.Install(routers.Get(router_idx), 
                                                            receivers.Get(receiver_base + 1));
        
        // Receiver 2: 3Mbps/16ms
        p2p.SetDeviceAttribute("DataRate", StringValue("3Mbps"));
        p2p.SetChannelAttribute("Delay", StringValue("16ms"));
        p2p.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("50p"));
        devRouterReceiver[receiver_base + 2] = p2p.Install(routers.Get(router_idx), 
                                                            receivers.Get(receiver_base + 2));
    }
    
    // Assign IP addresses
    Ipv4AddressHelper addr;
    
    // Server-Router links
    std::vector<Ipv4InterfaceContainer> ifaceServerRouter(5);
    for (uint32_t i = 0; i < 5; i++) {
        std::ostringstream subnet;
        subnet << "10.1." << (i + 1) << ".0";
        addr.SetBase(subnet.str().c_str(), "255.255.255.0");
        ifaceServerRouter[i] = addr.Assign(devServerRouter[i]);
    }
    
    // Router-Receiver links
    std::vector<Ipv4InterfaceContainer> ifaceRouterReceiver(15);
    for (uint32_t i = 0; i < 15; i++) {
        std::ostringstream subnet;
        subnet << "10.2." << (i + 1) << ".0";
        addr.SetBase(subnet.str().c_str(), "255.255.255.0");
        ifaceRouterReceiver[i] = addr.Assign(devRouterReceiver[i]);
    }
    
    // Populate routing tables
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    // Open cwnd trace files for first 3 flows
    std::string cwndFile1 = outputDir + tcpVariant + "_cwnd_flow1.csv";
    std::string cwndFile2 = outputDir + tcpVariant + "_cwnd_flow2.csv";
    std::string cwndFile3 = outputDir + tcpVariant + "_cwnd_flow3.csv";
    
    cwndStream1.open(cwndFile1);
    cwndStream2.open(cwndFile2);
    cwndStream3.open(cwndFile3);
    
    cwndStream1 << "Time,CongestionWindow" << std::endl;
    cwndStream2 << "Time,CongestionWindow" << std::endl;
    cwndStream3 << "Time,CongestionWindow" << std::endl;
    
    // Install applications (server sends MULTIPLE flows to each receiver)
    // Use 3 flows per receiver (45 total) to create congestion and show Reno's sawtooth
    uint16_t port = 9000;
    ApplicationContainer sinkApps;
    ApplicationContainer sourceApps;
    uint32_t flowId = 0;
    
    for (uint32_t i = 0; i < 15; i++) {
        // Create 3 flows per receiver to increase congestion
        for (uint32_t flowNum = 0; flowNum < 3; flowNum++) {
            // Sink on receiver
            PacketSinkHelper sink("ns3::TcpSocketFactory",
                                  InetSocketAddress(Ipv4Address::GetAny(), port + flowId));
            ApplicationContainer sinkApp = sink.Install(receivers.Get(i));
            sinkApp.Start(Seconds(0.0));
            sinkApp.Stop(Seconds(simulationTime));
            sinkApps.Add(sinkApp);
            
            // Source on server (bulk send)
            BulkSendHelper source("ns3::TcpSocketFactory",
                                  InetSocketAddress(ifaceRouterReceiver[i].GetAddress(1),
                                                    port + flowId));
            source.SetAttribute("MaxBytes", UintegerValue(0));  // unlimited
            source.SetAttribute("SendSize", UintegerValue(1400));
            
            ApplicationContainer sourceApp = source.Install(server);
            sourceApp.Start(Seconds(0.5 + flowId * 0.005));  // Stagger by 5ms
            sourceApp.Stop(Seconds(simulationTime));
            sourceApps.Add(sourceApp);
            
            flowId++;
        }
    }
    
    // Install FlowMonitor
    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> flowMonitor = flowmonHelper.InstallAll();
    
    // Connect cwnd tracers for flows to different receivers (heterogeneous paths)
    // Trace flows 0, 3, 6 to show 1Mbps, 2Mbps, 3Mbps paths respectively
    Simulator::Schedule(Seconds(1.0), []() {
        // Trace flow to Receiver 0 (1Mbps/50ms path) - Socket 0
        Config::ConnectWithoutContext("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow",
                                      MakeCallback(&CwndTracer1));
        // Trace flow to Receiver 1 (2Mbps/25ms path) - Socket 3
        Config::ConnectWithoutContext("/NodeList/0/$ns3::TcpL4Protocol/SocketList/3/CongestionWindow",
                                      MakeCallback(&CwndTracer2));
        // Trace flow to Receiver 2 (3Mbps/16ms path) - Socket 6
        Config::ConnectWithoutContext("/NodeList/0/$ns3::TcpL4Protocol/SocketList/6/CongestionWindow",
                                      MakeCallback(&CwndTracer3));
    });
    
    // Print topology info
    std::cout << "\n========================================" << std::endl;
    std::cout << "Fanout Topology TCP Simulation" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "TCP Variant: " << tcpVariant << std::endl;
    std::cout << "Topology: 1 server → 5 routers → 15 receivers" << std::endl;
    std::cout << "  Flows: 45 (3 flows per receiver for increased congestion)" << std::endl;
    std::cout << "  Server-Router: 6Mbps/100ms (bottleneck to trigger TCP sawtooth)" << std::endl;
    std::cout << "  Router-Receiver: 1Mbps/50ms, 2Mbps/25ms, 3Mbps/16ms (heterogeneous)" << std::endl;
    std::cout << "Simulation Time: " << simulationTime << " seconds" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    if (verbose) {
        LogComponentEnable("OgSim2", LOG_LEVEL_INFO);
    }
    
    // Run simulation
    std::cout << "Starting simulation..." << std::endl;
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();
    std::cout << "Simulation completed. Processing results..." << std::endl;
    
    // Collect flow statistics
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats();
    
    [[maybe_unused]] double totalTxBytes = 0;
    double totalRxBytes = 0;
    double totalDelay = 0;
    uint32_t totalTxPackets = 0;
    uint32_t totalRxPackets = 0;
    uint32_t flowCount = 0;
    
    // Store per-flow statistics for plotting
    std::vector<double> flowThroughputs;
    std::vector<double> flowDelays;
    
    std::cout << "\nFlow Statistics:" << std::endl;
    std::cout << std::string(90, '-') << std::endl;
    std::cout << std::setw(6) << "Flow" 
              << std::setw(20) << "Source" 
              << std::setw(20) << "Destination"
              << std::setw(15) << "Throughput"
              << std::setw(12) << "Loss"
              << std::setw(12) << "Delay" << std::endl;
    std::cout << std::string(90, '-') << std::endl;
    
    for (auto iter = stats.begin(); iter != stats.end(); ++iter) {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter->first);
        
        // Only count forward flows (server to receivers)
        if (t.sourceAddress.Get() == 0x0a010101) {  // From 10.1.1.1 (server)
            flowCount++;
            
            double duration = iter->second.timeLastRxPacket.GetSeconds() - 
                            iter->second.timeFirstTxPacket.GetSeconds();
            double throughput = (iter->second.rxBytes * 8.0) / duration / 1e6;  // Mbps
            double lossRate = (iter->second.txPackets - iter->second.rxPackets) * 100.0 / iter->second.txPackets;
            double avgDelay = iter->second.rxPackets > 0 ? 
                            iter->second.delaySum.GetMilliSeconds() / iter->second.rxPackets : 0;
            
            // Store per-flow stats
            flowThroughputs.push_back(throughput);
            flowDelays.push_back(avgDelay);
            
            totalTxBytes += iter->second.txBytes;
            totalRxBytes += iter->second.rxBytes;
            totalTxPackets += iter->second.txPackets;
            totalRxPackets += iter->second.rxPackets;
            totalDelay += avgDelay;
            
            std::cout << std::setw(6) << flowCount
                     << std::setw(20) << t.sourceAddress
                     << std::setw(20) << t.destinationAddress
                     << std::setw(12) << std::fixed << std::setprecision(2) << throughput << " Mbps"
                     << std::setw(11) << std::fixed << std::setprecision(1) << lossRate << "%"
                     << std::setw(11) << std::fixed << std::setprecision(2) << avgDelay << " ms"
                     << std::endl;
        }
    }
    
    std::cout << std::string(90, '-') << std::endl;
    
    // Aggregate statistics
    double simDuration = simulationTime - 0.5;  // Exclude startup
    double totalThroughput = (totalRxBytes * 8.0) / simDuration / 1e6;  // Mbps
    double avgThroughputPerFlow = flowCount > 0 ? totalThroughput / flowCount : 0;
    double avgDelay = flowCount > 0 ? totalDelay / flowCount : 0;
    uint32_t totalLost = totalTxPackets - totalRxPackets;
    double lossRate = totalTxPackets > 0 ? (totalLost * 100.0 / totalTxPackets) : 0;
    
    std::cout << "\nAggregate Statistics:" << std::endl;
    std::cout << "  Total Throughput: " << std::fixed << std::setprecision(2) << totalThroughput << " Mbps" << std::endl;
    std::cout << "  Average Throughput per Flow: " << std::fixed << std::setprecision(2) << avgThroughputPerFlow << " Mbps" << std::endl;
    std::cout << "  Average Delay: " << std::fixed << std::setprecision(2) << avgDelay << " ms" << std::endl;
    std::cout << "  Total Lost Packets: " << totalLost << std::endl;
    std::cout << "  Average Loss Rate: " << std::fixed << std::setprecision(2) << lossRate << "%" << std::endl;
    std::cout << "  Number of Flows: " << flowCount << std::endl;
    
    // Save aggregate stats to CSV
    std::string csvFilename = outputDir + tcpVariant + "_fanout.csv";
    std::ofstream csvFile(csvFilename);
    
    csvFile << "TCP_Variant,Total_Throughput_Mbps,Avg_Throughput_Per_Flow_Mbps,"
            << "Avg_Delay_ms,Total_Lost_Packets,Loss_Rate_Percent,Num_Flows" << std::endl;
    
    csvFile << tcpVariant << ","
            << std::fixed << std::setprecision(4) << totalThroughput << ","
            << avgThroughputPerFlow << ","
            << avgDelay << ","
            << totalLost << ","
            << lossRate << ","
            << flowCount << std::endl;
    
    csvFile.close();
    
    // Save per-flow stats to separate CSV for plotting
    std::string perFlowFilename = outputDir + tcpVariant + "_perflow.csv";
    std::ofstream perFlowFile(perFlowFilename);
    
    perFlowFile << "Flow_ID,Throughput_Mbps,Delay_ms" << std::endl;
    for (size_t i = 0; i < flowThroughputs.size(); i++) {
        perFlowFile << (i + 1) << ","
                   << std::fixed << std::setprecision(4) << flowThroughputs[i] << ","
                   << flowDelays[i] << std::endl;
    }
    
    perFlowFile.close();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results saved to: " << csvFilename << std::endl;
    std::cout << "Per-flow stats saved to: " << perFlowFilename << std::endl;
    std::cout << "Cwnd traces saved for flows 1-3" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // Close cwnd trace files
    cwndStream1.close();
    cwndStream2.close();
    cwndStream3.close();
    
    Simulator::Destroy();
}

int main(int argc, char* argv[])
{
    std::string tcpVariant = "Reno";
    uint32_t simulationTime = 60;  // 60 seconds for high-delay network
    bool verbose = false;
    std::string outputDir = "results/og-sim-2/";
    
    CommandLine cmd;
    cmd.AddValue("tcpVariant", "TCP variant (LinuxReno, Fast)", tcpVariant);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("verbose", "Enable verbose logging", verbose);
    cmd.AddValue("outputDir", "Output directory for results", outputDir);
    cmd.Parse(argc, argv);
    
    // Create output directory if it doesn't exist
    std::string mkdir_cmd = "mkdir -p " + outputDir;
    system(mkdir_cmd.c_str());
    
    RunSimulation(tcpVariant, simulationTime, verbose, outputDir);
    
    return 0;
}

