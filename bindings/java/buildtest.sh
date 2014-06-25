#!/bin/bash

cd ../..

g++ -shared -fPIC -o obj.master/libramcloudtest.so obj.master/AbstractLog.o obj.master/BackupClient.o obj.master/BackupFailureMonitor.o obj.master/BackupMasterRecovery.o obj.master/BackupSelector.o obj.master/BackupService.o obj.master/BackupStorage.o obj.master/CleanableSegmentManager.o obj.master/CoordinatorServerList.o obj.master/CoordinatorService.o obj.master/CoordinatorUpdateInfo.pb.o obj.master/CoordinatorUpdateManager.o obj.master/DataBlock.o obj.master/Enumeration.o obj.master/EnumerationIterator.o obj.master/EnumerationIterator.pb.o obj.master/FailureDetector.o obj.master/HashTable.o obj.master/InMemoryStorage.o obj.master/IndexletManager.o obj.master/Log.o obj.master/LogCleaner.o obj.master/LogDigest.o obj.master/LogEntryRelocator.o obj.master/LogIterator.o obj.master/MasterRecoveryManager.o obj.master/MasterService.o obj.master/MasterTableMetadata.o obj.master/MembershipService.o obj.master/MinCopysetsBackupSelector.o obj.master/MockCluster.o obj.master/MockDriver.o obj.master/MockExternalStorage.o obj.master/MockInfiniband.o obj.master/MockTransport.o obj.master/ObjectManager.o obj.master/OptionParser.o obj.master/PingService.o obj.master/PriorityTaskQueue.o obj.master/Recovery.o obj.master/RecoverySegmentBuilder.o obj.master/ReplicaManager.o obj.master/ReplicatedSegment.o obj.master/RuntimeOptions.o obj.master/SegmentIterator.o obj.master/SegmentManager.o obj.master/Server.o obj.master/ServerListEntry.pb.o obj.master/SideLog.o obj.master/SingleFileStorage.o obj.master/Table.pb.o obj.master/TableManager.o obj.master/TableManager.pb.o obj.master/TableStats.o obj.master/Tablet.o obj.master/TabletManager.o obj.master/TaskQueue.o obj.master/WallTime.o -ldl -lpcrecpp -lboost_program_options -lprotobuf -lrt -lboost_filesystem -lboost_system -lssl -lcrypto -rdynamic -libverbs

#g++ -shared -lpthread -o obj.master/libramcloud.so obj.master/AbstractServerList.o obj.master/Buffer.o obj.master/CRamCloud.o obj.master/ClientException.o obj.master/ClusterMetrics.o obj.master/CodeLocation.o obj.master/Context.o obj.master/CoordinatorClient.o obj.master/CoordinatorRpcWrapper.o obj.master/CoordinatorSession.o obj.master/Crc32C.o obj.master/Common.o obj.master/Cycles.o obj.master/Dispatch.o obj.master/Driver.o obj.master/ExternalStorage.o obj.master/FailSession.o obj.master/FastTransport.o obj.master/IndexLookup.o obj.master/IndexRpcWrapper.o obj.master/IpAddress.o obj.master/Key.o obj.master/LogEntryTypes.o obj.master/Logger.o obj.master/LargeBlockOfMemory.o obj.master/LogMetricsStringer.o obj.master/MacAddress.o obj.master/MasterClient.o obj.master/Memory.o obj.master/MultiOp.o obj.master/MultiRead.o obj.master/MultiRemove.o obj.master/MultiWrite.o obj.master/MurmurHash3.o obj.master/Object.o obj.master/ObjectBuffer.o obj.master/ObjectFinder.o obj.master/ObjectRpcWrapper.o obj.master/PcapFile.o obj.master/PerfCounter.o obj.master/PingClient.o obj.master/PortAlarm.o obj.master/RamCloud.o obj.master/RawMetrics.o obj.master/SegletAllocator.o obj.master/Seglet.o obj.master/Segment.o obj.master/RpcWrapper.o obj.master/ServerIdRpcWrapper.o obj.master/ServerList.o obj.master/ServerMetrics.o obj.master/ServerRpcPool.o obj.master/Service.o obj.master/ServiceLocator.o obj.master/ServiceManager.o obj.master/SessionAlarm.o obj.master/SpinLock.o obj.master/Status.o obj.master/StringUtil.o obj.master/TableEnumerator.o obj.master/TcpTransport.o obj.master/TestLog.o obj.master/ThreadId.o obj.master/TimeCounter.o obj.master/TimeTrace.o obj.master/Transport.o obj.master/TransportManager.o obj.master/UdpDriver.o obj.master/Util.o obj.master/WireFormat.o obj.master/WorkerSession.o obj.master/WorkerTimer.o obj.master/ZooStorage.o obj.master/Infiniband.o obj.master/InfRcTransport.o obj.master/InfUdDriver.o obj.master/Histogram.pb.o obj.master/LogMetrics.pb.o obj.master/MasterRecoveryInfo.pb.o obj.master/MetricList.pb.o obj.master/ServerConfig.pb.o obj.master/ServerList.pb.o obj.master/ServerStatistics.pb.o obj.master/SpinLockStatistics.pb.o obj.master/Tablets.pb.o obj.master/Indexlets.pb.o obj.master/RecoveryPartition.pb.o obj.master/TableConfig.pb.o -Wl,--no-undefined  /usr/local/lib/libzookeeper_mt.a -lpcrecpp -lboost_program_options -lprotobuf -lrt -lboost_filesystem -lboost_system -lpthread -lssl -lcrypto -rdynamic -libverbs