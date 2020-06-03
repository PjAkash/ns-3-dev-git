/*
 * Copyright (c) 2017 NITK Surathkal
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Nandita G <gm.nandita@gmail.com>
 *          Mohit P. Tahiliani <tahiliani@nitk.edu.in>
 *
 */

#include "ns3/log.h"
#include "ns3/enum.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/simulator.h"
#include "ns3/abort.h"
#include "choke-queue-disc.h"
#include "ns3/drop-from-queue.h"
#include "ns3/ipv4-packet-filter.h"
#include "ns3/ipv6-packet-filter.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("ChokeQueueDisc");

NS_OBJECT_ENSURE_REGISTERED (ChokeQueueDisc);

TypeId ChokeQueueDisc::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::ChokeQueueDisc")
    .SetParent<QueueDisc> ()
    .SetGroupName ("TrafficControl")
    .AddConstructor<ChokeQueueDisc> ()
    .AddAttribute ("Mode",
                   "Determines unit for QueueLimit",
                   EnumValue (QUEUE_DISC_MODE_PACKETS),
                   MakeEnumAccessor (&ChokeQueueDisc::SetMode),
                   MakeEnumChecker (QUEUE_DISC_MODE_BYTES, "QUEUE_DISC_MODE_BYTES",
                                    QUEUE_DISC_MODE_PACKETS, "QUEUE_DISC_MODE_PACKETS"))
    .AddAttribute ("MeanPktSize",
                   "Average of packet size",
                   UintegerValue (500),
                   MakeUintegerAccessor (&ChokeQueueDisc::m_meanPktSize),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Wait",
                   "True for waiting between dropped packets",
                   BooleanValue (true),
                   MakeBooleanAccessor (&ChokeQueueDisc::m_isWait),
                   MakeBooleanChecker ())
    .AddAttribute ("MinTh",
                   "Minimum average length threshold in packets/bytes",
                   DoubleValue (5),
                   MakeDoubleAccessor (&ChokeQueueDisc::m_minTh),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("MaxTh",
                   "Maximum average length threshold in packets/bytes",
                   DoubleValue (15),
                   MakeDoubleAccessor (&ChokeQueueDisc::m_maxTh),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("QueueLimit",
                   "Queue limit in bytes/packets",
                   UintegerValue (25),
                   MakeUintegerAccessor (&ChokeQueueDisc::SetQueueLimit),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("QW",
                   "Queue weight related to the exponential weighted moving average (EWMA)",
                   DoubleValue (0.002),
                   MakeDoubleAccessor (&ChokeQueueDisc::m_qW),
                   MakeDoubleChecker <double> ())
    .AddAttribute ("LInterm",
                   "The maximum probability of dropping a packet",
                   DoubleValue (50),
                   MakeDoubleAccessor (&ChokeQueueDisc::m_lInterm),
                   MakeDoubleChecker <double> ())
    .AddAttribute ("Ns1Compat",
                   "NS-1 compatibility",
                   BooleanValue (false),
                   MakeBooleanAccessor (&ChokeQueueDisc::m_isNs1Compat),
                   MakeBooleanChecker ())
    .AddAttribute ("LinkBandwidth",
                   "The CHOKe link bandwidth",
                   DataRateValue (DataRate ("1.5Mbps")),
                   MakeDataRateAccessor (&ChokeQueueDisc::m_linkBandwidth),
                   MakeDataRateChecker ())
    .AddAttribute ("LinkDelay",
                   "The CHOKe link delay",
                   TimeValue (MilliSeconds (20)),
                   MakeTimeAccessor (&ChokeQueueDisc::m_linkDelay),
                   MakeTimeChecker ())
    .AddAttribute ("UseEcn",
                   "True to use ECN (packets are marked instead of being dropped)",
                   BooleanValue (false),
                   MakeBooleanAccessor (&ChokeQueueDisc::m_useEcn),
                   MakeBooleanChecker ())
    .AddAttribute ("UseHardDrop",
                   "True to always drop packets above max threshold",
                   BooleanValue (true),
                   MakeBooleanAccessor (&ChokeQueueDisc::m_useHardDrop),
                   MakeBooleanChecker ())
   ;

  return tid;
}

ChokeQueueDisc::ChokeQueueDisc ()
  : QueueDisc ()
{
  NS_LOG_FUNCTION (this);
  m_uv = CreateObject<UniformRandomVariable> ();
  m_rnd = CreateObject<UniformRandomVariable> ();
}

ChokeQueueDisc::~ChokeQueueDisc ()
{
  NS_LOG_FUNCTION (this);
}

void
ChokeQueueDisc::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_uv = 0;
  m_rnd = 0;
  QueueDisc::DoDispose ();
}

void
ChokeQueueDisc::SetMode (QueueDiscMode mode)
{
  NS_LOG_FUNCTION (this << mode);
  m_mode = mode;
}

ChokeQueueDisc::QueueDiscMode
ChokeQueueDisc::GetMode (void)
{
  NS_LOG_FUNCTION (this);
  return m_mode;
}

void
ChokeQueueDisc::SetQueueLimit (uint32_t lim)
{
  NS_LOG_FUNCTION (this << lim);
  m_queueLimit = lim;
}

void
ChokeQueueDisc::SetTh (double minTh, double maxTh)
{
  NS_LOG_FUNCTION (this << minTh << maxTh);
  NS_ASSERT (minTh <= maxTh);
  m_minTh = minTh;
  m_maxTh = maxTh;
}


int64_t
ChokeQueueDisc::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_uv->SetStream (stream);
  m_rnd->SetStream (stream + 1);
  return 1;
}

bool
ChokeQueueDisc::DoEnqueue (Ptr<QueueDiscItem> item)
{
  NS_LOG_FUNCTION (this << item);

  uint32_t nQueued = 0;

  if (GetMode () == QUEUE_DISC_MODE_BYTES)
    {
      NS_LOG_DEBUG ("Enqueue in bytes mode");
      nQueued = GetInternalQueue (0)->GetNBytes ();
    }
  else if (GetMode () == QUEUE_DISC_MODE_PACKETS)
    {
      NS_LOG_DEBUG ("Enqueue in packets mode\n");
      nQueued = GetInternalQueue (0)->GetNPackets ();
    }

  // simulate number of packets arrival during idle period
  uint32_t m = 0;

  if (m_idle == true)
    {
      NS_LOG_DEBUG ("CHOKe Queue Disc is idle.");
      Time now = Simulator::Now ();
      m = uint32_t (m_ptc * (now - m_idleTime).GetSeconds ());
      m_idle = false;
    }

  m_qAvg = Estimator (nQueued, m + 1, m_qAvg, m_qW);

  NS_LOG_DEBUG ("\t bytesInQueue  " << GetInternalQueue (0)->GetNBytes () << "\tQavg " << m_qAvg);
  NS_LOG_DEBUG ("\t packetsInQueue  " << GetInternalQueue (0)->GetNPackets () << "\tQavg " << m_qAvg);
  m_count++;
  m_countBytes += item->GetSize ();

  uint32_t dropType = DTYPE_NONE;
  if (m_qAvg >= m_minTh && nQueued > 1)
    {
      m_rnd->SetAttribute ("Min", DoubleValue (1));
      m_rnd->SetAttribute ("Max", DoubleValue (nQueued - 1));
      
      uint32_t randompos = m_rnd->GetInteger ();
      Ptr<Queue<QueueDiscItem> > queue =  GetInternalQueue (0);
      Ptr<DropFromQueue<QueueDiscItem> > q = queue->GetObject<DropFromQueue<QueueDiscItem> > ();
      Ptr<const QueueDiscItem> randomit = q->PeekAt (randompos);
      QueueDiscItem * randomitem = const_cast<QueueDiscItem *> (GetPointer (randomit));

      int32_t hash = Classify (item);
      int32_t hashrnd = Classify (randomitem);
      
      if (hash == hashrnd)
        {
          DropBeforeEnqueue (item, FORCED_MARK);
          q->RemoveFrom (randompos);
          return false;
        }
    
      if (m_qAvg >= m_maxTh)
        {
          NS_LOG_DEBUG ("adding DROP FORCED MARK");
          dropType = DTYPE_FORCED;
        }
      else if (m_old == 0)
        {
          m_count = 1;
          m_countBytes = item->GetSize ();
          m_old = 1;
        }
      else if (DropEarly (item, nQueued))
        {
          NS_LOG_LOGIC ("DropEarly returns 1");
          dropType = DTYPE_UNFORCED;
        }
    }
  else
    {
      // No packets are being dropped
      m_vProb = 0.0;
      m_old = 0;
    }
  if (dropType == DTYPE_UNFORCED)
    {
      if (!m_useEcn || !Mark (item, UNFORCED_MARK))
        {
          DropBeforeEnqueue (item, UNFORCED_DROP);
          return false;
        }
    }
  else if (dropType == DTYPE_FORCED)
    {
      if (m_useHardDrop || !m_useEcn || !Mark (item, FORCED_MARK))
        {
          DropBeforeEnqueue (item, FORCED_DROP);
          if (m_isNs1Compat)
            {
              m_count = 0;
              m_countBytes = 0;
            }
          return false;
        }
    }

  bool retval = GetInternalQueue (0)->Enqueue (item);

  // If Queue::Enqueue fails, QueueDisc::Drop is called by the internal queue
  // because QueueDisc::AddInternalQueue sets the drop callback

  NS_LOG_LOGIC ("Number packets " << GetInternalQueue (0)->GetNPackets ());
  NS_LOG_LOGIC ("Number bytes " << GetInternalQueue (0)->GetNBytes ());

  return retval;
}

void
ChokeQueueDisc::InitializeParams (void)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_INFO ("Initializing CHOKe params.");
  m_ptc = m_linkBandwidth.GetBitRate () / (8.0 * m_meanPktSize);
  NS_ASSERT (m_minTh <= m_maxTh);

  m_qAvg = 0.0;
  m_count = 0;
  m_countBytes = 0;
  m_old = 0;
  m_idle = true;

  double th_diff = (m_maxTh - m_minTh);
  if (th_diff == 0)
    {
      th_diff = 1.0;
    }
  m_vA = 1.0 / th_diff;
  m_curMaxP = 1.0 / m_lInterm;
  m_vB = -m_minTh / th_diff;
  m_idleTime = NanoSeconds (0);

  NS_LOG_DEBUG ("\tm_delay " << m_linkDelay.GetSeconds () << "; m_isWait "
                             << m_isWait << "; m_qW " << m_qW << "; m_ptc " << m_ptc
                             << "; m_minTh " << m_minTh << "; m_maxTh " << m_maxTh
                             << "; th_diff " << th_diff
                             << "; lInterm " << m_lInterm << "; va " << m_vA <<  "; cur_max_p "
                             << m_curMaxP << "; v_b " << m_vB);
}
// Compute the average queue size
double
ChokeQueueDisc::Estimator (uint32_t nQueued, uint32_t m, double qAvg, double qW)
{
  NS_LOG_FUNCTION (this << nQueued << m << qAvg << qW);

  double newAve = qAvg * pow (1.0 - qW, m);
  newAve += qW * nQueued;
  return newAve;
}

// Check if packet p needs to be dropped due to probability mark
uint32_t
ChokeQueueDisc::DropEarly (Ptr<QueueDiscItem> item, uint32_t qSize)
{
  NS_LOG_FUNCTION (this << item << qSize);
  m_vProb1 = CalculatePNew (m_qAvg, m_maxTh, m_vA, m_vB, m_curMaxP);
  m_vProb = ModifyP (m_vProb1, m_count, m_countBytes, m_meanPktSize, m_isWait, item->GetSize ());

  // Drop probability is computed, pick random number and act
  double u = m_uv->GetValue ();

  if (u <= m_vProb)
    {
      NS_LOG_LOGIC ("u <= m_vProb; u " << u << "; m_vProb " << m_vProb);

      // DROP or MARK
      m_count = 0;
      m_countBytes = 0;

      return 1; // drop
    }

  return 0; // no drop/mark
}

// Returns a probability using these function parameters for the DropEarly funtion
double
ChokeQueueDisc::CalculatePNew (double qAvg, double maxTh, double vA,
                               double vB, double maxP)
{
  NS_LOG_FUNCTION (this << qAvg << maxTh  << vA << vB << maxP);
  double p;

  if (qAvg >= maxTh)
    {
      /*
       * OLD: p continues to range linearly above max_p as
       * the average queue size ranges above th_max.
       * NEW: p is set to 1.0
       */
      p = 1.0;
    }
  else
    {
      /*
       * p ranges from 0 to max_p as the average queue size ranges from
       * th_min to th_max
       */
      p = vA * qAvg + vB;
      p *= maxP;
    }

  if (p > 1.0)
    {
      p = 1.0;
    }

  return p;
}

// Returns a probability using these function parameters for the DropEarly funtion
double
ChokeQueueDisc::ModifyP (double p, uint32_t count, uint32_t countBytes,
                         uint32_t meanPktSize, bool isWait, uint32_t size)
{
  NS_LOG_FUNCTION (this << p << count << countBytes << meanPktSize << isWait << size);
  double count1 = (double) count;

  if (GetMode () == QUEUE_DISC_MODE_BYTES)
    {
      count1 = (double) (countBytes / meanPktSize);
    }

  if (isWait)
    {
      if (count1 * p < 1.0)
        {
          p = 0.0;
        }
      else if (count1 * p < 2.0)
        {
          p /= (2.0 - count1 * p);
        }
      else
        {
          p = 1.0;
        }
    }
  else
    {
      if (count1 * p < 1.0)
        {
          p /= (1.0 - count1 * p);
        }
      else
        {
          p = 1.0;
        }
    }

  if ((GetMode () == QUEUE_DISC_MODE_BYTES) && (p < 1.0))
    {
      p = (p * size) / meanPktSize;
    }

  if (p > 1.0)
    {
      p = 1.0;
    }

  return p;
}

uint32_t
ChokeQueueDisc::GetQueueSize (void)
{
  NS_LOG_FUNCTION (this);
  if (GetMode () == QUEUE_DISC_MODE_BYTES)
    {
      return GetInternalQueue (0)->GetNBytes ();
    }
  else if (GetMode () == QUEUE_DISC_MODE_PACKETS)
    {
      return GetInternalQueue (0)->GetNPackets ();
    }
  else
    {
      NS_ABORT_MSG ("Unknown CHOKe mode.");
    }
}

Ptr<QueueDiscItem>
ChokeQueueDisc::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_DEBUG ("inside deq");
  if (GetInternalQueue (0)->IsEmpty ())
    {
      NS_LOG_LOGIC ("Queue empty");
      return 0;
    }
  else
    {
      m_idle = false;
      Ptr<QueueDiscItem> item = GetInternalQueue (0)->Dequeue ();

      NS_LOG_LOGIC ("Popped " << item);

      NS_LOG_LOGIC ("Number packets " << GetInternalQueue (0)->GetNPackets ());
      NS_LOG_LOGIC ("Number bytes " << GetInternalQueue (0)->GetNBytes ());
      if (GetInternalQueue (0)->IsEmpty ())
        {
          m_idle = true;
          m_idleTime = Simulator::Now ();
        }
      return item;
    }
}

Ptr<const QueueDiscItem>
ChokeQueueDisc::DoPeek (void) const
{
  NS_LOG_FUNCTION (this);
  if (GetInternalQueue (0)->IsEmpty ())
    {
      NS_LOG_LOGIC ("Queue empty");
      return 0;
    }

  Ptr<const QueueDiscItem> item = GetInternalQueue (0)->Peek ();

  NS_LOG_LOGIC ("Number packets " << GetInternalQueue (0)->GetNPackets ());
  NS_LOG_LOGIC ("Number bytes " << GetInternalQueue (0)->GetNBytes ());

  return item;
}

bool
ChokeQueueDisc::CheckConfig (void)
{
  NS_LOG_FUNCTION (this);
  if (GetNQueueDiscClasses () > 0)
    {
      NS_LOG_ERROR ("ChokeQueueDisc cannot have classes");
      return false;
    }

  if (GetNPacketFilters () < 1)
    {
      NS_LOG_ERROR ("ChokeQueueDisc should have atleast one packet filter");
      return false;
    }

  if (GetNInternalQueues () == 0)
    {
      // add a DropFrom queue
      AddInternalQueue (CreateObjectWithAttributes<DropFromQueue<QueueDiscItem> >
                          ("MaxSize", QueueSizeValue (GetMaxSize ())));
    }

  if (GetNInternalQueues () != 1)
    {
      NS_LOG_ERROR ("ChokeQueueDisc needs 1 internal queue");
      return false;
    }

  // if ((GetInternalQueue (0)-> GetMaxSize ().GetUnit () != QueueSizeUnit::PACKETS && m_mode == QUEUE_DISC_MODE_BYTES)
  //     || (GetInternalQueue (0)-> GetMaxSize ().GetUnit () != QueueSizeUnit::BYTES && m_mode == QUEUE_DISC_MODE_PACKETS))
  //   {
  //     NS_LOG_ERROR ("The mode of the provided queue does not match the mode set on the ChokeQueueDisc");
  //     return false;
  //   }

  // if ((m_mode ==  QUEUE_DISC_MODE_PACKETS && GetInternalQueue (0)->GetMaxPackets () < m_queueLimit)
  //     || (m_mode ==  QUEUE_DISC_MODE_BYTES && GetInternalQueue (0)->GetMaxBytes () < m_queueLimit))
  //   {
  //     NS_LOG_ERROR ("The size of the internal queue is less than the queue disc limit");
  //     return false;
  //   }

  return true;
}

} // namespace ns3
