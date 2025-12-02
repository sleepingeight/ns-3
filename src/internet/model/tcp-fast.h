
 #ifndef TCP_FAST_H
 #define TCP_FAST_H
 
 #include "tcp-congestion-ops.h"
 
 namespace ns3
 {
 
 /**
  * \ingroup congestionOps
  *
  * \brief An implementation of TCP FAST (improved version matching NS-2)
  *
  * TCP FAST (Fast Active-queue management Scalable Transmission Control Protocol)
  * is a delay-based congestion control algorithm. It uses queueing delay as a
  * congestion signal and adjusts the congestion window to maintain a target
  * number of packets in the network queues.
  *
  * Key features:
  * - Uses baseRTT (minimum observed RTT) as propagation delay
  * - Uses queueing delay (avgRTT - baseRTT) as congestion signal  
  * - Target window = (baseRTT/avgRTT) * cwnd + alpha
  * - Smoothed update: new_cwnd = (1-γ) * current_cwnd + γ * target
  * - MI (Multiplicative Increase) mode for very low delays
  * - Alpha and Beta thresholds for triggering updates
  *
  * Reference: Jin, C., Wei, D. X., & Low, S. H. (2004). FAST TCP: Motivation,
  * architecture, algorithms, performance. IEEE Infocom.
  */
 class TcpFast : public TcpNewReno
 {
   public:
     /**
      * \brief Get the type ID.
      * \return the object TypeId
      */
     static TypeId GetTypeId();
 
     /**
      * Create an unbound tcp socket.
      */
     TcpFast();
 
     /**
      * \brief Copy constructor
      * \param sock the object to copy
      */
     TcpFast(const TcpFast& sock);
 
     /**
      * \brief Destructor
      */
     ~TcpFast() override;
 
     std::string GetName() const override;
 
     /**
      * \brief Get slow start threshold following packet loss
      *
      * \param tcb internal congestion state
      * \param bytesInFlight bytes in flight
      *
      * \return the slow start threshold value
      */
     uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight) override;
 
     Ptr<TcpCongestionOps> Fork() override;
 
     /**
      * \brief Trigger events/calculations specific to a congestion state
      *
      * \param tcb internal congestion state
      * \param newState new congestion state to which the TCP is going to switch
      */
     void CongestionStateSet(Ptr<TcpSocketState> tcb,
                            const TcpSocketState::TcpCongState_t newState) override;
 
     /**
      * \brief Adjust cwnd following FAST TCP algorithm
      *
      * \param tcb internal congestion state
      * \param segmentsAcked count of segments acked
      */
     void IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) override;
 
     /**
      * \brief Perform RTT sampling needed to execute FAST algorithm
      *
      * The function filters RTT samples from the last RTT to find
      * the minimum and base RTT values.
      *
      * \param tcb internal congestion state
      * \param segmentsAcked count of segments acked
      * \param rtt last RTT
      */
     void PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt) override;
 
     /**
      * \brief Enable FAST algorithm to start taking FAST samples
      *
      * FAST algorithm is enabled in the following situations:
      * 1. at the establishment of a connection
      * 2. after an RTO
      * 3. after fast recovery
      * 4. when an idle connection is restarted
      *
      * \param tcb internal congestion state
      */
     void EnableFast(Ptr<TcpSocketState> tcb);
 
     /**
      * \brief Stop taking FAST samples
      */
     void DisableFast();
 
   protected:
   private:
     uint32_t m_alpha;              //!< Lower bound of packets in network (buffering target)
     uint32_t m_beta;               //!< Upper bound multiplier for queue occupancy check
     double m_gamma;                 //!< Smoothing factor (weight for new value)
     Time m_miThreshold;            //!< MI (Multiplicative Increase) threshold
     Time m_baseRtt;                //!< Minimum of all RTTs measured (propagation delay)
     Time m_minRtt;                 //!< Minimum RTT in current measurement period
     uint32_t m_cntRtt;             //!< Number of RTT samples in current period
     bool m_doingFastNow;           //!< If true, FAST is enabled
     SequenceNumber32 m_begSndNxt;  //!< Right edge of measurement window
     uint32_t m_lastCwnd;           //!< Cwnd from last update cycle (old_cwnd)
 };
 
 } // namespace ns3
 
 #endif // TCP_FAST_H
 
 