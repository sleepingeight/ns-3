#include "tcp-fast-1.h"

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
                                          "Lower bound of packets in network",
                                          UintegerValue(250),
                                          MakeUintegerAccessor(&TcpFast::m_alpha),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("Gamma",
                                          "Smoothing Factor",
                                          DoubleValue(0.5),
                                          MakeDoubleAccessor(&TcpFast::m_gamma),
                                          MakeDoubleChecker<double>());
    return tid;
}

TcpFast::TcpFast()
    : TcpNewReno(),
    m_alpha(250),
    m_gamma(0.5),
    m_baseRtt(Time::Max()),
    m_totRtt(0),
    m_cntRtt(0),
    m_doingFastNow(false),
    m_begSndNxt(0)
{
    NS_LOG_FUNCTION(this);
}

TcpFast::TcpFast(const TcpFast& sock)
    : TcpNewReno(sock),
    m_alpha(sock.m_alpha),
    m_gamma(sock.m_gamma),
    m_baseRtt(sock.m_baseRtt),
    m_totRtt(sock.m_totRtt),
    m_cntRtt(sock.m_cntRtt),
    m_doingFastNow(sock.m_doingFastNow),
    m_begSndNxt(sock.m_begSndNxt)
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

    if (rtt.IsZero()) return;

    m_baseRtt = std::min(m_baseRtt, rtt);
    NS_LOG_DEBUG("Updated m_baseRtt = " << m_baseRtt);

    // BUG FIX: Update total RTT for average calculation
    m_totRtt += rtt;
    m_cntRtt++;
    NS_LOG_DEBUG("Updated m_totRtt = " << m_totRtt << ", m_cntRtt = " << m_cntRtt);
}

void
TcpFast::EnableFast(Ptr<TcpSocketState> tcb)
{
    NS_LOG_FUNCTION(this << tcb);
    m_doingFastNow = true;
    m_begSndNxt = tcb->m_nextTxSequence;
    m_cntRtt = 0;
    m_baseRtt = Time::Max();
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

    if (tcb -> m_lastAckedSeq >= m_begSndNxt)
    {
        // An entire window's acknowledgements are recevied, we do Fast cwnd adjustment.
        NS_LOG_LOGIC("A Fast cycle has finished, we adjust cwnd per RTT.");

        // Save the current right edge for next Vegas cycle
        m_begSndNxt = tcb->m_nextTxSequence;

        // Fast calculations only is case of enough RTT samples to not get misleaded by delayed ACK
        if (m_cntRtt <= 2)
        {
            NS_LOG_LOGIC("Insufficient RTT samples to do Fast, so behave like Reno");
            TcpNewReno::IncreaseWindow(tcb, segmentsAcked);
        }
        else
        {
            NS_LOG_LOGIC("Sufficient RTT samples to do Fast");
            // Compute average RTT for the cycle
            double avgRttSec = m_totRtt.GetSeconds() / static_cast<double>(m_cntRtt);
            double baseRttSec = m_baseRtt.GetSeconds();

            // Clear RTT stats for next cycle
            m_totRtt = Seconds(0.0);
            m_cntRtt = 0;

            uint32_t segCwnd = tcb->GetCwndInSegments();
            auto cwndD = static_cast<double>(segCwnd);

            // Eq. (6): target window = (baseRTT / avgRTT) * cwnd + alpha
            double target = (baseRttSec / avgRttSec) * cwndD + static_cast<double>(m_alpha);

            // Apply FAST control law: cwnd = (1 - γ)*old + γ*target
            double newCwndD = (1.0 - m_gamma) * cwndD + m_gamma * target;

            // uint32_t newCwndSegs = static_cast<uint32_t>(std::max(newCwndD, 2.0));
            uint32_t newCwndSegs = static_cast<uint32_t>(std::min(newCwndD, 3.0*segCwnd));
            if (tcb->m_cWnd < tcb->m_ssThresh)
            { // Slow start mode
                NS_LOG_LOGIC("We are in slow start and diff < m_gamma, so we "
                             "follow NewReno slow start");
                TcpNewReno::SlowStart(tcb, segmentsAcked);
            }
            else {
                NS_LOG_LOGIC("We are in linear increase/decrease mode");
                tcb->m_cWnd = newCwndSegs * tcb->m_segmentSize;
            }

            tcb->m_ssThresh = std::max(tcb->m_ssThresh, 3 * tcb->m_cWnd / 4);
            NS_LOG_DEBUG("Updated ssThresh = " << tcb->m_ssThresh);
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
    return std::max(std::min(tcb->m_ssThresh.Get(), tcb->m_cWnd.Get() - tcb->m_segmentSize),
                    2 * tcb->m_segmentSize);
}
} // namespace ns3
