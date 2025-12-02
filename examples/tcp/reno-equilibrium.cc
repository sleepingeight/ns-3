#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/error-model.h"
#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("RenoEquilibrium");

std::ofstream cwndFile;

// Custom error model that drops exactly one packet at specified time
class SinglePacketErrorModel : public ErrorModel
{
public:
    static TypeId GetTypeId ()
    {
        static TypeId tid = TypeId ("SinglePacketErrorModel")
            .SetParent<ErrorModel> ()
            .AddConstructor<SinglePacketErrorModel> ();
        return tid;
    }

    SinglePacketErrorModel () : m_dropTime (1.0), m_dropped (false) {}
    
    void SetDropTime (double t) { m_dropTime = t; }

private:
    bool DoCorrupt (Ptr<Packet> p) override
    {
        double now = Simulator::Now ().GetSeconds ();
        if (!m_dropped && now >= m_dropTime)
        {
            m_dropped = true;
            std::cout << "*** PACKET DROPPED at t=" << now << "s ***" << std::endl;
            return true; // Drop this packet
        }
        return false; // Don't drop
    }

    void DoReset () override { m_dropped = false; }

    double m_dropTime;
    bool m_dropped;
};

uint32_t g_cwnd = 0;

static void
CwndTrace (uint32_t oldCwnd, uint32_t newCwnd)
{
    g_cwnd = newCwnd;
}

static void
PrintCwnd ()
{
    cwndFile << Simulator::Now ().GetSeconds () << " " << g_cwnd << std::endl;
    Simulator::Schedule (Seconds (0.1), &PrintCwnd);
}

int main (int argc, char *argv[])
{
    Time::SetResolution(Time::NS);
    LogComponentEnable("RenoEquilibrium", LOG_LEVEL_INFO);

    // Open output file
    cwndFile.open("results/reno-equilibrium/cwnd_trace.txt");
    if (!cwndFile.is_open())
    {
        std::cerr << "Error: Could not open output file!" << std::endl;
        return 1;
    }

    
    // 1. Nodes
    NodeContainer nodes;
    nodes.Create(2);

    
    // 2. Point-to-Point link
    // 7.2 Gbps, 100 ms RTT (50 ms each way)
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("7.2Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("50ms"));
    p2p.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("100000p"));

    NetDeviceContainer devices = p2p.Install(nodes);

    
    // 3. Single packet loss at t=1 second
    Ptr<SinglePacketErrorModel> errorModel = CreateObject<SinglePacketErrorModel>();
    errorModel->SetDropTime(1.0); // Drop exactly one packet at t=1 second
    devices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(errorModel));

    
    // 4. Install Internet + TCP
    // Use TCP NewReno and configure TCP BEFORE installing stack
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
        TypeIdValue(TcpNewReno::GetTypeId()));
    
    // Configure TCP to allow large windows (for 60k packets ~ 90MB)
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448)); // MSS
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(200000000)); // 200 MB
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(200000000)); // 200 MB
    Config::SetDefault("ns3::TcpSocketBase::WindowScaling", BooleanValue(true));
    
    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifs = address.Assign(devices);

    
    // 5. BulkSend (long-lived flow)
    uint16_t port = 5000;
    Address sinkAddress(InetSocketAddress(ifs.GetAddress(1), port));

    BulkSendHelper source("ns3::TcpSocketFactory", sinkAddress);
    source.SetAttribute("MaxBytes", UintegerValue(0)); // infinite
    ApplicationContainer sourceApp = source.Install(nodes.Get(0));
    sourceApp.Start(Seconds(0.1));
    sourceApp.Stop(Seconds(2000)); // long simulation

    // Sink
    PacketSinkHelper sink("ns3::TcpSocketFactory",
                          InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sink.Install(nodes.Get(1));
    sinkApp.Start(Seconds(0.0));

    
    // 6. Trace cwnd every 0.1 seconds
    Simulator::Schedule(Seconds(0.11), [&sourceApp]() {
        Ptr<BulkSendApplication> app = DynamicCast<BulkSendApplication>(sourceApp.Get(0));
        Ptr<Socket> socket = app->GetSocket();
        socket->TraceConnectWithoutContext("CongestionWindow", MakeCallback(&CwndTrace));
        Simulator::Schedule (Seconds (0.0), &PrintCwnd);
    });

    
    // 7. Run Simulation
    std::cout << "Starting simulation..." << std::endl;
    Simulator::Stop(Seconds(0));
    Simulator::Run();
    Simulator::Destroy();
    
    cwndFile.close();
    std::cout << "\nSimulation complete! Results written to results/reno-equilibrium/cwnd_trace.txt" << std::endl;
    
    return 0;
}
