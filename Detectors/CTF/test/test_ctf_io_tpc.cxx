// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#define BOOST_TEST_MODULE Test TPCCTFIO
#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#undef NDEBUG
#include <cassert>

#include <boost/test/unit_test.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/data/dataset.hpp>
#include "DataFormatsTPC/CompressedClusters.h"
#include "DataFormatsTPC/CTF.h"
#include "CommonUtils/NameConf.h"
#include "TPCReconstruction/CTFCoder.h"
#include "Framework/Logger.h"
#include <TFile.h>
#include <TRandom.h>
#include <TStopwatch.h>
#include <cstring>

using namespace o2::tpc;
namespace boost_data = boost::unit_test::data;

inline std::vector<o2::ctf::ANSHeader> ANSVersions{o2::ctf::ANSVersionCompat, o2::ctf::ANSVersion1};
inline std::vector<bool> CombineColumns(true, false);

BOOST_DATA_TEST_CASE(CTFTest, boost_data::make(ANSVersions) ^ boost_data::make(CombineColumns), ansVersion, combineColumns)
{
  CompressedClusters c;
  c.nAttachedClusters = 99;
  c.nUnattachedClusters = 88;
  c.nAttachedClustersReduced = 77;
  c.nTracks = 66;

  std::vector<char> bVec;
  CompressedClustersFlat* ccFlat = nullptr;
  size_t sizeCFlatBody = CTFCoder::alignSize(ccFlat);
  size_t sz = sizeCFlatBody + CTFCoder::estimateSize(c);
  bVec.resize(sz);
  ccFlat = reinterpret_cast<CompressedClustersFlat*>(bVec.data());
  auto buff = reinterpret_cast<void*>(reinterpret_cast<char*>(bVec.data()) + sizeCFlatBody);
  {
    CTFCoder coder(o2::ctf::CTFCoderBase::OpType::Encoder);
    coder.setCompClusAddresses(c, buff);
    coder.setCombineColumns(combineColumns);
  }
  ccFlat->set(sz, c);

  // fill some data
  for (int i = 0; i < c.nUnattachedClusters; i++) {
    c.qTotU[i] = i;
    c.qMaxU[i] = i;
    c.flagsU[i] = i;
    c.padDiffU[i] = i;
    c.timeDiffU[i] = i;
    c.sigmaPadU[i] = i;
    c.sigmaTimeU[i] = i;
  }
  for (int i = 0; i < c.nAttachedClusters; i++) {
    c.qTotA[i] = i;
    c.qMaxA[i] = i;
    c.flagsA[i] = i;
    c.sigmaPadA[i] = i;
    c.sigmaTimeA[i] = i;
  }
  for (int i = 0; i < c.nAttachedClustersReduced; i++) {
    c.rowDiffA[i] = i;
    c.sliceLegDiffA[i] = i;
    c.padResA[i] = i;
    c.timeResA[i] = i;
  }
  for (int i = 0; i < c.nTracks; i++) {
    c.qPtA[i] = i;
    c.rowA[i] = i;
    c.sliceA[i] = i;
    c.timeA[i] = i;
    c.padA[i] = i;
    c.nTrackClusters[i] = i;
  }
  for (int i = 0; i < c.nSliceRows; i++) {
    c.nSliceRowClusters[i] = i;
  }

  TStopwatch sw;
  sw.Start();
  std::vector<o2::ctf::BufferType> vecIO;
  {
    CTFCoder coder(o2::ctf::CTFCoderBase::OpType::Decoder);
    coder.setCombineColumns(combineColumns);
    coder.setANSVersion(ansVersion);
    coder.encode(vecIO, c, c); // compress
  }
  sw.Stop();
  LOG(info) << "Compressed in " << sw.CpuTime() << " s";

  // writing
  {
    sw.Start();
    TFile flOut("test_ctf_tpc.root", "recreate");
    TTree ctfTree(std::string(o2::base::NameConf::CTFTREENAME).c_str(), "O2 CTF tree");
    auto* ctfImage = o2::tpc::CTF::get(vecIO.data());
    ctfImage->print();
    ctfImage->appendToTree(ctfTree, "TPC");
    ctfTree.Write();
    sw.Stop();
    LOG(info) << "Wrote to tree in " << sw.CpuTime() << " s";
  }

  // reading
  vecIO.clear();
  {
    sw.Start();
    TFile flIn("test_ctf_tpc.root");
    std::unique_ptr<TTree> tree((TTree*)flIn.Get(std::string(o2::base::NameConf::CTFTREENAME).c_str()));
    BOOST_CHECK(tree);
    o2::tpc::CTF::readFromTree(vecIO, *(tree.get()), "TPC");
    sw.Stop();
    LOG(info) << "Read back from tree in " << sw.CpuTime() << " s";
  }

  std::vector<char> vecIn;
  sw.Start();
  const auto ctfImage = o2::tpc::CTF::getImage(vecIO.data());
  {
    CTFCoder coder(o2::ctf::CTFCoderBase::OpType::Decoder);
    coder.setCombineColumns(true);
    coder.decode(ctfImage, vecIn); // decompress
  }
  sw.Stop();
  LOG(info) << "Decompressed in " << sw.CpuTime() << " s";
  //
  // compare with original flat clusters
  BOOST_CHECK(vecIn.size() == bVec.size());
  const CompressedClustersCounters* countOrig = reinterpret_cast<const CompressedClustersCounters*>(bVec.data());
  const CompressedClustersCounters* countDeco = reinterpret_cast<const CompressedClustersCounters*>(vecIn.data());
  BOOST_CHECK(countOrig->nTracks == countDeco->nTracks);
  BOOST_CHECK(countOrig->nAttachedClusters == countDeco->nAttachedClusters);
  BOOST_CHECK(countOrig->nUnattachedClusters == countDeco->nUnattachedClusters);
  BOOST_CHECK(countOrig->nAttachedClustersReduced == countDeco->nAttachedClustersReduced);
  BOOST_CHECK(countOrig->nSliceRows == countDeco->nSliceRows);
  BOOST_CHECK(countOrig->nComppressionModes == countDeco->nComppressionModes);
  BOOST_CHECK(countOrig->solenoidBz == countDeco->solenoidBz);
  BOOST_CHECK(countOrig->maxTimeBin == countDeco->maxTimeBin);
  BOOST_CHECK(memcmp(vecIn.data() + sizeof(o2::tpc::CompressedClustersCounters), bVec.data() + sizeof(o2::tpc::CompressedClustersCounters), bVec.size() - sizeof(o2::tpc::CompressedClustersCounters)) == 0);
}
