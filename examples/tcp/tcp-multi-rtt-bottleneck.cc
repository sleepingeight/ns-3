#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpMultiRttBottleneck");

// Structure to track flow statistics
struct FlowStats
{
    uint64_t bytesReceived = 0;
    Time startTime;
    Time endTime;
    uint32_t rttMs;
    uint16_t port;
    double totalDelay = 0.0;  // Sum of all delays
    uint32_t packetCount = 0;  // Number of packets for delay calculation
    uint32_t txPackets = 0;  // Transmitted packets
    uint32_t rxPackets = 0;  // Received packets
};

std::map<uint16_t, FlowStats> flowStatsMap;
std::string tcpVariant = "TcpNewReno";
std::string outputDir = "results/tcp-multi-rtt/";
std::map<uint32_t, Ptr<OutputStreamWrapper>> cwndStreams;  // Per-flow cwnd output

// Callback to track received bytes per flow
void
FlowRxTrace(uint16_t port, Ptr<const Packet> packet, const Address& from)
{
    flowStatsMap[port].bytesReceived += packet->GetSize();
    flowStatsMap[port].rxPackets++;
}

// Callback to track transmitted packets per flow
void
FlowTxTrace(uint16_t port, Ptr<const Packet> packet)
{
    flowStatsMap[port].txPackets++;
}

// Trace congestion window for each flow
void
CwndChange(uint32_t flowId, Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
    *stream->GetStream() << Simulator::Now().GetSeconds() << " " 
                         << newCwnd << std::endl;
}

// Trace RTT to measure delay
void
RttChange(uint16_t port, Time oldRtt, Time newRtt)
{
    flowStatsMap[port].totalDelay += newRtt.GetSeconds();
    flowStatsMap[port].packetCount++;
}

// Helper function to connect traces after sockets are created
void
ConnectTraces(uint32_t numFlows, uint16_t basePort, NodeContainer senders)
{
    for (uint32_t i = 0; i < numFlows; i++)
    {
        Ptr<Node> node = senders.Get(i);
        
        // Use Config::Connect to trace all TCP sockets on this node
        std::ostringstream oss;
        oss << "/NodeList/" << node->GetId() << "/$ns3::TcpL4Protocol/SocketList/*/CongestionWindow";
        Config::ConnectWithoutContext(oss.str(), MakeBoundCallback(&CwndChange, i, cwndStreams[i]));
        
        // Trace RTT
        uint16_t port = basePort + i;
        std::ostringstream ossRtt;
        ossRtt << "/NodeList/" << node->GetId() << "/$ns3::TcpL4Protocol/SocketList/*/RTT";
        Config::ConnectWithoutContext(ossRtt.str(), MakeBoundCallback(&RttChange, port));
    }
}

int
main(int argc, char* argv[])
{
    // Simulation parameters
    uint32_t numFlows = 4;  // Number of flows with different RTTs
    std::vector<uint32_t> rttValues = {50, 100, 150, 200};  // in milliseconds
    std::string bottleneckBandwidth = "100Mbps";  // Reduced for faster sim
    std::string bottleneckDelay = "1ms";
    uint32_t bottleneckQueueSize = 200;  // packets (reduced)
    uint32_t dataSize = 0;  // 0 means unlimited (run for fixed time)
    double simulationTime = 180.0;  // seconds 
    uint32_t segmentSize = 1448;
    bool enablePcap = false;
    
    // Parse command line
    CommandLine cmd;
    cmd.AddValue("tcpVariant", "TCP variant (TcpNewReno, TcpVegas, TcpFast)", tcpVariant);
    cmd.AddValue("numFlows", "Number of flows", numFlows);
    cmd.AddValue("bandwidth", "Bottleneck bandwidth", bottleneckBandwidth);
    cmd.AddValue("queueSize", "Bottleneck queue size in packets", bottleneckQueueSize);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("outputDir", "Output directory", outputDir);
    cmd.AddValue("enablePcap", "Enable pcap traces", enablePcap);
    cmd.Parse(argc, argv);
    
    // Ensure we have enough RTT values
    while (rttValues.size() < numFlows)
    {
        rttValues.push_back(rttValues.back() + 50);
    }
    
    // Create output directory
    std::string command = "mkdir -p " + outputDir;
    [[maybe_unused]] int result = system(command.c_str());
    
    // Configure TCP variant
    if (tcpVariant == "TcpNewReno")
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
    }
    else if (tcpVariant == "TcpVegas")
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpVegas"));
    }
    else if (tcpVariant == "TcpFast")
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpFast"));
    }
    else if (tcpVariant == "TcpLinuxReno")
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpLinuxReno"));
    }
    
    // TCP configuration
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(segmentSize));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(10 * 1024 * 1024));  // 10 MB
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(10 * 1024 * 1024));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocketBase::WindowScaling", BooleanValue(true));  // Enable window scaling
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "TCP Multi-RTT Bottleneck Simulation" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "TCP Variant: " << tcpVariant << std::endl;
    std::cout << "Number of Flows: " << numFlows << std::endl;
    std::cout << "Bottleneck: " << bottleneckBandwidth << std::endl;
    std::cout << "Queue Size: " << bottleneckQueueSize << " packets" << std::endl;
    std::cout << "RTT Values: ";
    for (uint32_t i = 0; i < numFlows; i++)
    {
        std::cout << rttValues[i] << "ms ";
    }
    std::cout << std::endl;
    std::cout << "Simulation Time: " << simulationTime << " seconds" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // Create nodes
    NodeContainer senders;
    senders.Create(numFlows);
    
    Ptr<Node> router = CreateObject<Node>();
    Ptr<Node> receiver = CreateObject<Node>();
    
    // Install Internet stack
    InternetStackHelper internet;
    internet.Install(senders);
    internet.Install(router);
    internet.Install(receiver);
    
    // Create point-to-point links from senders to router with different delays
    std::vector<NetDeviceContainer> senderLinks(numFlows);
    PointToPointHelper p2pSenders;
    p2pSenders.SetDeviceAttribute("DataRate", StringValue("800Mbps"));  // High bandwidth access links
    p2pSenders.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1000p"));
    
    Ipv4AddressHelper ipv4;
    std::vector<Ipv4InterfaceContainer> senderInterfaces(numFlows);
    
    for (uint32_t i = 0; i < numFlows; i++)
    {
        // Set delay for this sender-router link (half of RTT)
        uint32_t oneWayDelay = rttValues[i];
        p2pSenders.SetChannelAttribute("Delay", TimeValue(MilliSeconds(oneWayDelay)));
        
        // Create link
        NodeContainer senderRouterPair(senders.Get(i), router);
        senderLinks[i] = p2pSenders.Install(senderRouterPair);
        
        // Assign IP addresses
        std::ostringstream subnet;
        subnet << "10.1." << (i + 1) << ".0";
        ipv4.SetBase(subnet.str().c_str(), "255.255.255.0");
        senderInterfaces[i] = ipv4.Assign(senderLinks[i]);
    }
    
    // Create bottleneck link: router to receiver
    PointToPointHelper p2pBottleneck;
    p2pBottleneck.SetDeviceAttribute("DataRate", StringValue(bottleneckBandwidth));
    p2pBottleneck.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    
    // Install traffic control with DropTail queue at bottleneck
    TrafficControlHelper tchBottleneck;
    tchBottleneck.SetRootQueueDisc("ns3::FifoQueueDisc", 
                                     "MaxSize", StringValue(std::to_string(bottleneckQueueSize) + "p"));
    
    NodeContainer routerReceiverPair(router, receiver);
    NetDeviceContainer bottleneckLink = p2pBottleneck.Install(routerReceiverPair);
    
    // Install traffic control on router's interface to receiver
    QueueDiscContainer queueDiscs = tchBottleneck.Install(bottleneckLink.Get(0));
    
    // Assign IP to bottleneck link
    ipv4.SetBase("10.2.1.0", "255.255.255.0");
    Ipv4InterfaceContainer bottleneckInterfaces = ipv4.Assign(bottleneckLink);
    
    // Enable routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    // Set up applications - one flow per sender with different port numbers
    uint16_t basePort = 5000;
    ApplicationContainer sourceApps;
    ApplicationContainer sinkApps;
    
    // Set up ASCII trace helper for cwnd output
    AsciiTraceHelper asciiTraceHelper;
    
    for (uint32_t i = 0; i < numFlows; i++)
    {
        uint16_t port = basePort + i;
        
        // Install sink on receiver
        Address sinkAddress(InetSocketAddress(bottleneckInterfaces.GetAddress(1), port));
        PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", sinkAddress);
        ApplicationContainer sinkApp = sinkHelper.Install(receiver);
        sinkApp.Start(Seconds(0.0));
        sinkApp.Stop(Seconds(simulationTime));
        sinkApps.Add(sinkApp);
        
        // Connect Rx trace
        Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApp.Get(0));
        sink->TraceConnectWithoutContext("Rx", MakeBoundCallback(&FlowRxTrace, port));
        
        // Install bulk send on sender
        BulkSendHelper sourceHelper("ns3::TcpSocketFactory", sinkAddress);
        sourceHelper.SetAttribute("MaxBytes", UintegerValue(dataSize));  // 0 = unlimited
        sourceHelper.SetAttribute("SendSize", UintegerValue(segmentSize));
        ApplicationContainer sourceApp = sourceHelper.Install(senders.Get(i));
        
        // Stagger start times slightly to avoid perfect synchronization
        sourceApp.Start(Seconds(0.1 + i * 0.05));
        sourceApp.Stop(Seconds(simulationTime));
        sourceApps.Add(sourceApp);
        
        // Connect Tx trace to track transmitted packets
        Ptr<Application> app = sourceApp.Get(0);
        app->TraceConnectWithoutContext("Tx", MakeBoundCallback(&FlowTxTrace, port));
        
        // Initialize flow stats
        flowStatsMap[port].startTime = Seconds(0.1 + i * 0.05);
        flowStatsMap[port].endTime = Seconds(simulationTime);
        flowStatsMap[port].rttMs = rttValues[i];
        flowStatsMap[port].port = port;
        
        // Set up cwnd trace file for this flow
        std::string cwndTraceFile = outputDir + tcpVariant + "_flow" + std::to_string(i) + "_cwnd.dat";
        cwndStreams[i] = asciiTraceHelper.CreateFileStream(cwndTraceFile);
        *cwndStreams[i]->GetStream() << "# Time(s) Cwnd(segments)" << std::endl;
    }
    
    // Schedule trace connections after TCP sockets are created
    // Sockets are created when applications start
    Simulator::Schedule(Seconds(0.5), &ConnectTraces, numFlows, basePort, senders);
    
    // Enable pcap if requested
    if (enablePcap)
    {
        p2pBottleneck.EnablePcap(outputDir + tcpVariant + "_bottleneck", bottleneckLink.Get(0), true);
    }
    
    std::cout << "Starting simulation..." << std::endl;
    
    // Run simulation
    Simulator::Stop(Seconds(simulationTime + 1));
    Simulator::Run();
    
    std::cout << "Simulation completed. Processing results..." << std::endl;
    
    // Calculate and display results
    std::string resultsFile = outputDir + tcpVariant + "_results.csv";
    std::ofstream outFile(resultsFile);
    outFile << "TCP_Variant,Flow_ID,RTT_ms,Port,Throughput_Mbps,Data_Received_MB,Avg_Delay_ms,Tx_Packets,Rx_Packets,Loss_Rate_Percent" << std::endl;
    
    double totalThroughput = 0.0;
    double minThroughput = 1e9;
    double maxThroughput = 0.0;
    
    std::cout << "\nPer-Flow Results:" << std::endl;
    std::cout << std::string(110, '-') << std::endl;
    std::cout << "Flow | RTT(ms) | Port | Throughput(Mbps) | Data Received(MB) | Avg Delay(ms) | Loss%" << std::endl;
    std::cout << std::string(110, '-') << std::endl;
    
    for (uint32_t i = 0; i < numFlows; i++)
    {
        uint16_t port = basePort + i;
        FlowStats& stats = flowStatsMap[port];
        
        double duration = (stats.endTime - stats.startTime).GetSeconds();
        double throughputMbps = (stats.bytesReceived * 8.0) / duration / 1e6;
        double dataMB = stats.bytesReceived / 1e6;
        double avgDelayMs = stats.packetCount > 0 ? 
                            (stats.totalDelay / stats.packetCount) * 1000.0 : 0.0;
        double lossRate = stats.txPackets > 0 ? 
                         ((stats.txPackets - stats.rxPackets) * 100.0 / stats.txPackets) : 0.0;
        
        totalThroughput += throughputMbps;
        minThroughput = std::min(minThroughput, throughputMbps);
        maxThroughput = std::max(maxThroughput, throughputMbps);
        
        // Write to CSV
        outFile << tcpVariant << "," << i << "," << stats.rttMs << "," 
                << port << "," << throughputMbps << "," << dataMB << "," 
                << avgDelayMs << "," << stats.txPackets << "," << stats.rxPackets << ","
                << lossRate << std::endl;
        
        // Print to console
        std::cout << std::setw(4) << i << " | " 
                  << std::setw(7) << stats.rttMs << " | "
                  << std::setw(4) << port << " | "
                  << std::fixed << std::setprecision(2) << std::setw(16) << throughputMbps << " | "
                  << std::setw(18) << dataMB << " | "
                  << std::setw(13) << avgDelayMs << " | "
                  << std::setw(5) << lossRate << std::endl;
    }
    
    outFile.close();
    
    // Calculate fairness index (Jain's Fairness Index)
    double sumThroughput = totalThroughput;
    double sumSquaredThroughput = 0.0;
    for (uint32_t i = 0; i < numFlows; i++)
    {
        uint16_t port = basePort + i;
        FlowStats& stats = flowStatsMap[port];
        double duration = (stats.endTime - stats.startTime).GetSeconds();
        double throughputMbps = (stats.bytesReceived * 8.0) / duration / 1e6;
        sumSquaredThroughput += throughputMbps * throughputMbps;
    }
    double fairnessIndex = (sumThroughput * sumThroughput) / (numFlows * sumSquaredThroughput);
    
    std::cout << std::string(70, '-') << std::endl;
    std::cout << "\nAggregate Statistics:" << std::endl;
    std::cout << "  Total Throughput: " << std::fixed << std::setprecision(2) 
              << totalThroughput << " Mbps" << std::endl;
    std::cout << "  Average Throughput: " << totalThroughput / numFlows << " Mbps" << std::endl;
    std::cout << "  Min Throughput: " << minThroughput << " Mbps" << std::endl;
    std::cout << "  Max Throughput: " << maxThroughput << " Mbps" << std::endl;
    std::cout << "  Fairness Index (Jain): " << std::setprecision(4) << fairnessIndex << std::endl;
    
    // Extract bandwidth value for utilization calculation
    double bandwidthMbps = 100.0;  // Default, matches bottleneckBandwidth
    std::cout << "  Link Utilization: " << std::setprecision(2) 
              << (totalThroughput / bandwidthMbps * 100) << "%" << std::endl;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results saved to:" << std::endl;
    std::cout << "  " << resultsFile << std::endl;
    for (uint32_t i = 0; i < numFlows; i++)
    {
        std::string cwndFile = outputDir + tcpVariant + "_flow" + std::to_string(i) + "_cwnd.dat";
        std::cout << "  " << cwndFile << std::endl;
    }
    std::cout << "========================================\n" << std::endl;
    
    Simulator::Destroy();
    
    return 0;
}

