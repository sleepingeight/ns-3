#ifndef TCPFAST
#define TCPFAST

#include "tcp-congestion-ops.h"
#include <cstdint>

namespace ns3
{
class TcpSocketState;

/**
 * @ingroup congestionOps
 *
 * @brief An implementation of TCP Fast
 *
 * TCP Fast is a delay based congestion control algorithm similar to TCP Vegas.
 * The difference between TCP Vegas and FAST TCP lies in the way in which the rate is adjusted when
 * the number of packets stored is too small or large. TCP Vegas makes fixed size adjustments to the rate,
 * independent of how far the current rate is from the target rate. FAST TCP makes larger steps when the system is further from equilibrium and smaller steps near equilibrium.
 * This improves the speed of convergence and the stability.
 *
 * The window update is determined by the control law (Equation 5 in the paper):
 * New CWND = (1 - gamma) * CWND + gamma * ((minRTT / avgRTT) * CWND + alpha)
 *
 * where:
 * - w (CWND) is the Congestion Window.
 * - gamma is a weighting factor (gain), typically in (0, 1].
 * - (minRTT or baseRTT) is the minimum RTT observed.
 * - (avgRTT) is the measured average RTT.
 * - alpha is the target queue depth in packets (protocol parameter) = 2
 */

class TcpFast : public TcpNewReno
{
    public:
        /**
         * @brief Get the type ID.
         * @return the object TypeId
         */
        static TypeId GetTypeId();

        /**
         * Create an unbound tcp socket.
         */
        TcpFast();

        /**
         * @brief Copy Constructor
         */
        TcpFast(const TcpFast& sock);
        ~TcpFast() override;

        std::string GetName() const override;

        /**
         * @brief Compute RTTs needed to execute Fast algorithm
         *
         * This function finds out the minimum RTT, avg RTT seen in the runtime.
         *
         * @param tcb internal congestion state
         * @param segmentsAcked count of segments ACKed
         * @param rtt last RTT
         *
         */
        void PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt) override;

        /**
         * @brief Enable/disable Fast Algorithm depending on congestion state
         *
         * Fast is only implemented in case of normal socket state, i.e no loss recovery, etc.
         * This is in lines with the simulation in by the authors.
         * Although we would like to use the same congestion control
         * function during loss recovery, we have currently disabled this
         * feature because of ambiguities associated with retransmitted
         * packets. Currently when a packet loss is detected, FAST halves
         * its window and enters loss recovery. The goal is to back off
         * packet transmission quickly when severe congestion occurs,
         * in order to bring the system back to a regime where reliable
         * RTT measurements are again available for window adjustment to work effectively.
         */
        void CongestionStateSet(Ptr<TcpSocketState> tcb,
                                const TcpSocketState::TcpCongState_t newState) override;

        void IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) override;

        uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight) override;

        Ptr<TcpCongestionOps> Fork() override;
    protected:
    private:
    void EnableFast(Ptr<TcpSocketState> tcb);
    void DisableFast();

    private:
        uint32_t m_alpha;       //!< Alpha threshold, lower bound of packets in network
        double m_gamma;       //!< Gamma threshold, weighting factor
        Time m_baseRtt;         //!< Minimum of all RTT measurements seen during connection
        Time m_totRtt;          //!< Sum of all RTTs seen during connection (used to find avgRTT)
        uint32_t m_cntRtt;      //!< Number of RTTs seen
        bool m_doingFastNow;    //!< If true, do Fast
        SequenceNumber32 m_begSndNxt; //!< Right edge during last RTT
    };
}



#endif // TCPFAST
