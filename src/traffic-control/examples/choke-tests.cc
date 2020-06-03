#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/point-to-point-layout-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("ChokeTests");

uint32_t checkTimes;
double avgQueueSize;
std::stringstream filePlotQueue;
std::stringstream filePlotQueueAvg;

void
CheckQueueSize (Ptr<QueueDisc> queue)
{
  uint32_t qSize;
  qSize = StaticCast<ChokeQueueDisc> (queue)->GetQueueSize ();
  avgQueueSize += qSize;
  checkTimes++;

  Simulator::Schedule (Seconds (0.01), &CheckQueueSize, queue);

  std::ofstream fPlotQueue (filePlotQueue.str ().c_str (), std::ios::out | std::ios::app);
  fPlotQueue << Simulator::Now ().GetSeconds () << " " << qSize << std::endl;
  fPlotQueue.close ();

  std::ofstream fPlotQueueAvg (filePlotQueueAvg.str ().c_str (), std::ios::out | std::ios::app);
  fPlotQueueAvg << Simulator::Now ().GetSeconds () << " " << avgQueueSize / checkTimes << std::endl;
  fPlotQueueAvg.close ();
}

int
main (int argc, char *argv[])
{
  std::string pathOut;

  bool writeForPlot = false;
  bool writePcap = true;
  bool printStats = true;
  std::string queueDiscType = "CHOKe";

  CommandLine cmd;
  cmd.AddValue ("pathOut", "Path to save results from --writeForPlot/--writePcap", pathOut);
  cmd.AddValue ("writePcap", "<0/1> to write results in pcapfile", writePcap);
  cmd.AddValue ("writeForPlot", "<0/1> to write results for plot (gnuplot)", writeForPlot);
  cmd.AddValue ("queueDiscType", "queueDiscType", queueDiscType);
  cmd.Parse (argc, argv);

  if ((queueDiscType != "RED") && (queueDiscType != "PfifoFast") && (queueDiscType != "CHOKe"))
    {
      NS_ABORT_MSG ("Invalid queue disc type: Use --queueDiscType=RED or --queueDiscType=PfifoFast");
    }

  pathOut = ".";
  LogComponentEnable ("ChokeQueueDisc", LOG_LEVEL_INFO);
  LogComponentEnable ("RedQueueDisc", LOG_LEVEL_INFO);

  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::TcpNewReno"));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1000));
  Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (1));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (false));

  Ptr<QueueDisc> m_queue;
  TrafficControlHelper tch;

  if (queueDiscType == "RED")
    {
      NS_LOG_INFO ("Set RED params");
      Config::SetDefault ("ns3::RedQueueDisc::Mode", StringValue ("QUEUE_DISC_MODE_PACKETS"));
      Config::SetDefault ("ns3::RedQueueDisc::MeanPktSize", UintegerValue (1000));
      Config::SetDefault ("ns3::RedQueueDisc::Wait", BooleanValue (true));
      Config::SetDefault ("ns3::RedQueueDisc::Gentle", BooleanValue (false));
      Config::SetDefault ("ns3::RedQueueDisc::QW", DoubleValue (0.002));
      Config::SetDefault ("ns3::RedQueueDisc::MinTh", DoubleValue (100));
      Config::SetDefault ("ns3::RedQueueDisc::MaxTh", DoubleValue (200));
      Config::SetDefault ("ns3::RedQueueDisc::QueueLimit", UintegerValue (300));
      tch.SetRootQueueDisc ("ns3::RedQueueDisc", "LinkBandwidth", StringValue ("1Mbps"),"LinkDelay",StringValue ("1ms"));
    }
  else if (queueDiscType == "CHOKe")
    {
      NS_LOG_INFO ("Set CHOKE params");
      Config::SetDefault ("ns3::ChokeQueueDisc::Mode", StringValue ("QUEUE_DISC_MODE_PACKETS"));
      Config::SetDefault ("ns3::ChokeQueueDisc::MeanPktSize", UintegerValue (500));
      Config::SetDefault ("ns3::ChokeQueueDisc::Wait", BooleanValue (true));
      Config::SetDefault ("ns3::ChokeQueueDisc::QW", DoubleValue (0.002));
      Config::SetDefault ("ns3::ChokeQueueDisc::MinTh", DoubleValue (5));
      Config::SetDefault ("ns3::ChokeQueueDisc::MaxTh", DoubleValue (15));
      Config::SetDefault ("ns3::ChokeQueueDisc::QueueLimit", UintegerValue (30));
      uint16_t chokehandle = tch.SetRootQueueDisc ("ns3::ChokeQueueDisc", "LinkBandwidth", StringValue ("1Mbps"), "LinkDelay", StringValue ("1ms"));
      tch.AddPacketFilter (chokehandle,"ns3::FqCoDelQueueDisc");

    }
  else if (queueDiscType == "PfifoFast")
    {
      Config::SetDefault ("ns3::PfifoFastQueueDisc::Limit", UintegerValue (300));
      uint16_t handle = tch.SetRootQueueDisc ("ns3::PfifoFastQueueDisc");
      tch.AddInternalQueues (handle, 3, "ns3::DropTailQueue", "MaxPackets", UintegerValue (300));
    }

  NS_LOG_INFO ("Create channels");

  PointToPointHelper p2p;
  p2p.SetQueue ("ns3::DropTailQueue");
  p2p.SetDeviceAttribute ("DataRate", StringValue ("10Mbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));

  PointToPointHelper p2pBottleneck;
  if (queueDiscType == "RED" || queueDiscType == "PfifoFast")
    {
      p2pBottleneck.SetQueue ("ns3::DropTailQueue");
    }
  else if (queueDiscType == "CHOKe")
    {
      p2pBottleneck.SetQueue ("ns3::DropFromQueue");
    }

  p2pBottleneck.SetDeviceAttribute ("DataRate", StringValue ("1Mbps"));
  p2pBottleneck.SetChannelAttribute ("Delay", StringValue ("1ms"));
  PointToPointDumbbellHelper d (33, p2p, 33, p2p, p2pBottleneck);

  // Install Stack
  InternetStackHelper stack;
  d.InstallStack (stack);

  Ptr<QueueDisc> q1 = tch.Install (d.GetLeft ()->GetDevice (0)).Get (0);
  Ptr<QueueDisc> q2 = tch.Install (d.GetRight ()->GetDevice (0)).Get (0);

  Ptr<PointToPointNetDevice> m_device = d.GetLeft ()->GetDevice (0)->GetObject<PointToPointNetDevice> ();
  d.AssignIpv4Addresses (Ipv4AddressHelper ("10.1.1.0", "255.255.255.0"),
                         Ipv4AddressHelper ("10.2.1.0", "255.255.255.0"),
                         Ipv4AddressHelper ("10.3.1.0", "255.255.255.0"));

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  uint32_t port1 = 5000;
  uint32_t port2 = 9;

  ApplicationContainer sourceAndSinkApp;
  uint32_t i;
  for (i = 0; i < d.RightCount () - 1; ++i)
    {
      AddressValue remoteAddress (InetSocketAddress (d.GetRightIpv4Address (i), port1));
      BulkSendHelper ftp ("ns3::TcpSocketFactory", Address ());
      ftp.SetAttribute ("Remote", remoteAddress);
      ftp.SetAttribute ("MaxBytes", UintegerValue (0));

      sourceAndSinkApp.Add (ftp.Install (d.GetLeft (i)));
      PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (d.GetRightIpv4Address (i), port1));
      sinkHelper.SetAttribute ("Protocol", TypeIdValue (TcpSocketFactory::GetTypeId ()));
      sourceAndSinkApp.Add (sinkHelper.Install (d.GetRight (i)));
    }

  OnOffHelper onoff ("ns3::UdpSocketFactory",Address (InetSocketAddress (d.GetRightIpv4Address (i), port2)));
  onoff.SetConstantRate (DataRate ("2Mb/s"));
  ApplicationContainer apps = onoff.Install (d.GetLeft (i));
  apps.Start (Seconds (10.0));
  apps.Stop (Seconds (120.0));

  PacketSinkHelper sink ("ns3::UdpSocketFactory",Address (InetSocketAddress (Ipv4Address::GetAny (), port2)));

  apps = sink.Install (d.GetRight (i));
  apps.Start (Seconds (10.0));
  apps.Stop (Seconds (120.0));
  sourceAndSinkApp.Start (Seconds (10.0));
  sourceAndSinkApp.Stop (Seconds (120.0));

  if (writeForPlot)
    {
      filePlotQueue << pathOut << "/" << "queue.plotme";
      filePlotQueueAvg << pathOut << "/" << "queue_avg.plotme";

      remove (filePlotQueue.str ().c_str ());
      remove (filePlotQueueAvg.str ().c_str ());
      Simulator::ScheduleNow (&CheckQueueSize, q1);
    }

  if (writePcap)
    {
      std::stringstream stmp;
      stmp << pathOut << "/choke_tests_pcap";
      p2pBottleneck.EnablePcap (stmp.str ().c_str (), m_device);
    }

  Simulator::Stop (Seconds (120.0));
  Simulator::Run ();

  if (printStats)
    {
      QueueDisc::Stats st = q1->GetStats ();
      std::cout << "*** stats from left router queue disc ***" << std::endl;
      std::cout << st << std::endl;
    }

  Simulator::Destroy ();
  return 0;
}
