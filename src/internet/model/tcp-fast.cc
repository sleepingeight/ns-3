
 #include "tcp-fast.h"

 #include "tcp-congestion-ops.h"
 #include "tcp-socket-state.h"
 
 #include "ns3/log.h"
 #include <cstddef>
 #include <cstdint>
 #include <string>
 
 namespace ns3
 {
 
 NS_LOG_COMPONENT_DEFINE("TcpFast");
 NS_OBJECT_ENSURE_REGISTERED(TcpFast);
 
 TypeId
 TcpFast::GetTypeId()
 {
     static TypeId tid = TypeId("ns3::TcpFast")
                             .SetParent<TcpNewReno>()
                             .AddConstructor<TcpFast>()
                             .SetGroupName("Internet")
                             .AddAttribute("Alpha",
                                           "Lower bound of packets in network (number of buffered packets)",
                                           UintegerValue(200),
                                           MakeUintegerAccessor(&TcpFast::m_alpha),
                                           MakeUintegerChecker<uint32_t>())
                             .AddAttribute("Beta",
                                           "Upper bound multiplier for queue occupancy check",
                                           UintegerValue(400),
                                           MakeUintegerAccessor(&TcpFast::m_beta),
                                           MakeUintegerChecker<uint32_t>())
                             .AddAttribute("Gamma",
                                           "Smoothing Factor (weight for new value in EWMA)",
                                           DoubleValue(0.5),
                                           MakeDoubleAccessor(&TcpFast::m_gamma),
                                           MakeDoubleChecker<double>(0.0, 1.0))
                             .AddAttribute("MiThreshold",
                                           "MI threshold for queueing delay (seconds). Below this, use MI mode.",
                                           TimeValue(MilliSeconds(1000)),
                                           MakeTimeAccessor(&TcpFast::m_miThreshold),
                                           MakeTimeChecker());
     return tid;
 }
 
 TcpFast::TcpFast()
     : TcpNewReno(),
     m_alpha(200),
     m_beta(400),
     m_gamma(0.5),
     m_miThreshold(MilliSeconds(10)),
     m_baseRtt(Time::Max()),
     m_minRtt(Time::Max()),
     m_cntRtt(0),
     m_doingFastNow(false),
     m_begSndNxt(0),
     m_lastCwnd(0)
 {
     NS_LOG_FUNCTION(this);
 }
 
 TcpFast::TcpFast(const TcpFast& sock)
     : TcpNewReno(sock),
     m_alpha(sock.m_alpha),
     m_beta(sock.m_beta),
     m_gamma(sock.m_gamma),
     m_miThreshold(sock.m_miThreshold),
     m_baseRtt(sock.m_baseRtt),
     m_minRtt(sock.m_minRtt),
     m_cntRtt(sock.m_cntRtt),
     m_doingFastNow(sock.m_doingFastNow),
     m_begSndNxt(sock.m_begSndNxt),
     m_lastCwnd(sock.m_lastCwnd)
 {
     NS_LOG_FUNCTION(this);
 }
 
 TcpFast::~TcpFast()
 {
     NS_LOG_FUNCTION(this);
 }
 
 Ptr<TcpCongestionOps>
 TcpFast::Fork()
 {
     return CopyObject<TcpFast>(this);
 }
 
 void
 TcpFast::PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt)
 {
     NS_LOG_FUNCTION(this << tcb << segmentsAcked << rtt);
 
     if (rtt.IsZero()) 
         return;
 
     // Update baseRTT - minimum RTT observed
     m_baseRtt = std::min(m_baseRtt, rtt);
     NS_LOG_DEBUG("Updated m_baseRtt = " << m_baseRtt);
 
     // Also track current minimum for this measurement period
     m_minRtt = std::min(m_minRtt, rtt);
     
     // Count RTT samples for average calculation
     m_cntRtt++;
     NS_LOG_DEBUG("m_cntRtt = " << m_cntRtt);
 }
 
 void
 TcpFast::EnableFast(Ptr<TcpSocketState> tcb)
 {
     NS_LOG_FUNCTION(this << tcb);
     m_doingFastNow = true;
     m_begSndNxt = tcb->m_nextTxSequence;
     m_cntRtt = 0;
     m_minRtt = Time::Max();
     m_lastCwnd = tcb->m_cWnd.Get();  // Save current cwnd as "old" cwnd
 }
 
 void
 TcpFast::DisableFast()
 {
     NS_LOG_FUNCTION(this);
     m_doingFastNow = false;
 }
 
 void
 TcpFast::CongestionStateSet(Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCongState_t newState)
 {
     NS_LOG_FUNCTION(this << tcb << newState);
     if (newState == TcpSocketState::CA_OPEN)
     {
         EnableFast(tcb);
     }
     else
     {
         DisableFast();
     }
 }
 
 void
 TcpFast::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
 {
     NS_LOG_FUNCTION(this << tcb << segmentsAcked);
 
     if (!m_doingFastNow) {
         // If Fast is not on, NewReno Algo is implemented
         NS_LOG_LOGIC("Fast is not turned on, NewReno is followed.");
         TcpNewReno::IncreaseWindow(tcb, segmentsAcked);
         return;
     }
 
     // Check if an entire window's acknowledgements have been received
     if (tcb->m_lastAckedSeq >= m_begSndNxt)
     {
         NS_LOG_LOGIC("A Fast cycle has finished, we adjust cwnd per RTT.");
 
         // Save the current right edge for next Fast cycle
         m_begSndNxt = tcb->m_nextTxSequence;
 
         // Need at least 3 RTT samples to avoid issues with delayed ACKs
         if (m_cntRtt <= 2)
         {
             NS_LOG_LOGIC("Insufficient RTT samples to do Fast, so behave like NewReno");
             TcpNewReno::IncreaseWindow(tcb, segmentsAcked);
             m_cntRtt = 0;
             m_minRtt = Time::Max();
         }
         else
         {
             NS_LOG_LOGIC("Sufficient RTT samples (" << m_cntRtt << ") to do Fast");
             
             // Calculate average RTT for this measurement period
             Time avgRtt = m_minRtt;  // Approximation: use minimum RTT in the period
             Time baseRtt = m_baseRtt;
 
             if (baseRtt.IsZero() || avgRtt.IsZero())
             {
                 NS_LOG_WARN("BaseRTT or AvgRTT is zero, skipping FAST calculation");
                 TcpNewReno::IncreaseWindow(tcb, segmentsAcked);
                 m_cntRtt = 0;
                 m_minRtt = Time::Max();
                 return;
             }
 
             double baseRttSec = baseRtt.GetSeconds();
             double avgRttSec = avgRtt.GetSeconds();
             double queueingDelay = avgRttSec - baseRttSec;
 
             uint32_t currentCwnd = tcb->m_cWnd.Get();
             uint32_t oldCwnd = m_lastCwnd;  // Cwnd from last RTT cycle
             double currentCwndSegs = static_cast<double>(currentCwnd) / tcb->m_segmentSize;
             double oldCwndSegs = static_cast<double>(oldCwnd) / tcb->m_segmentSize;
 
             NS_LOG_DEBUG("Current cwnd=" << currentCwndSegs << " segs, Old cwnd=" << oldCwndSegs 
                         << " segs, BaseRTT=" << baseRttSec << "s, AvgRTT=" << avgRttSec 
                         << "s, QueueDelay=" << queueingDelay << "s");
 
             // Calculate queue occupancy: q = old_cwnd * queueing_delay
             double cwndArrayQ = oldCwndSegs * queueingDelay;
             double alphaTime = static_cast<double>(m_alpha) * avgRttSec;
             double betaTime = static_cast<double>(m_beta) * avgRttSec;
 
             NS_LOG_DEBUG("cwnd_array_q=" << cwndArrayQ << ", alpha*avgRTT=" << alphaTime 
                         << ", beta*avgRTT=" << betaTime);
 
             // Check for MI (Multiplicative Increase) mode
             // If queueing delay is very small, just increment cwnd
             if (queueingDelay < m_miThreshold.GetSeconds())
             {
                 NS_LOG_LOGIC("Queueing delay (" << queueingDelay << ") < MI threshold ("
                             << m_miThreshold.GetSeconds() << "), using MI mode");
                 
                 if (tcb->m_cWnd < tcb->m_ssThresh)
                 {
                     // Still in slow start
                     TcpNewReno::SlowStart(tcb, segmentsAcked);
                 }
                 else
                 {
                     // Congestion avoidance: just increment by 1 segment
                     tcb->m_cWnd += tcb->m_segmentSize;
                 }
             }
             // Check if we need to apply FAST control
             else if (cwndArrayQ < alphaTime || cwndArrayQ >= betaTime)
             {
                 NS_LOG_LOGIC("Applying FAST TCP control law");
                 
                 // FAST TCP control law (from NS-2 line 281):
                 // target_cwnd = (1-γ) * current_cwnd + γ * (old_cwnd * (baseRTT/avgRTT) + α)
                 double targetCwndSegs = (1.0 - m_gamma) * currentCwndSegs +
                                        m_gamma * (oldCwndSegs * (baseRttSec / avgRttSec) + static_cast<double>(m_alpha));
 
                 // Enforce minimum of 2 segments (from NS-2 line 286)
                 if (targetCwndSegs < 2.0)
                 {
                     targetCwndSegs = 2.0;
                 }
 
                 uint32_t newCwnd = static_cast<uint32_t>(targetCwndSegs * tcb->m_segmentSize);
 
                 NS_LOG_DEBUG("FAST calculation: target=" << targetCwndSegs << " segments, newCwnd=" 
                             << newCwnd << " bytes");
 
                 if (tcb->m_cWnd < tcb->m_ssThresh)
                 {
                     // Still in slow start phase
                     NS_LOG_LOGIC("In slow start, using NewReno slow start");
                     TcpNewReno::SlowStart(tcb, segmentsAcked);
                 }
                 else
                 {
                     // Congestion avoidance: apply FAST TCP
                     NS_LOG_LOGIC("In congestion avoidance, applying FAST cwnd");
                     tcb->m_cWnd = newCwnd;
                 }
 
                 // Update ssThresh to prevent it from dropping too low
                 // This maintains at least 75% of current cwnd (from NS-3 line 184)
                 tcb->m_ssThresh = std::max(tcb->m_ssThresh, 3 * tcb->m_cWnd / 4);
                 NS_LOG_DEBUG("Updated ssThresh = " << tcb->m_ssThresh);
             }
             else
             {
                 NS_LOG_LOGIC("Queue occupancy in stable range, no cwnd adjustment");
             }
 
             // Save current cwnd as "old" for next cycle
             m_lastCwnd = tcb->m_cWnd.Get();
             
             // Reset measurement counters for next cycle
             m_cntRtt = 0;
             m_minRtt = Time::Max();
         }
     }
 }
 
 std::string
 TcpFast::GetName() const
 {
     return "TcpFast";
 }
 
 uint32_t
 TcpFast::GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
 {
     NS_LOG_FUNCTION(this << tcb << bytesInFlight);
     
     // On congestion (packet loss), reduce ssthresh
     // Use the minimum of current ssthresh and (cwnd - 1 segment)
     // But ensure it's at least 2 segments
     return std::max(std::min(tcb->m_ssThresh.Get(), tcb->m_cWnd.Get() - tcb->m_segmentSize),
                     2 * tcb->m_segmentSize);
 }
 
 } // namespace ns3
 
 