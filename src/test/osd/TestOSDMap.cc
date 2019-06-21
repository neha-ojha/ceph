// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
#include "gtest/gtest.h"
#include "osd/OSDMap.h"
#include "osd/OSDMapMapping.h"

#include "global/global_context.h"
#include "global/global_init.h"
#include "common/common_init.h"
#include "common/ceph_argparse.h"

#include <iostream>

using namespace std;

int main(int argc, char **argv) {
  std::vector<const char*> args(argv, argv+argc);
  env_to_vec(args);
  auto cct = global_init(nullptr, args, CEPH_ENTITY_TYPE_CLIENT,
			 CODE_ENVIRONMENT_UTILITY,
			 CINIT_FLAG_NO_DEFAULT_CONFIG_FILE);
  common_init_finish(g_ceph_context);
  // make sure we have 3 copies, or some tests won't work
  g_ceph_context->_conf->set_val("osd_pool_default_size", "3", false);
  // our map is flat, so just try and split across OSDs, not hosts or whatever
  g_ceph_context->_conf->set_val("osd_crush_chooseleaf_type", "0", false);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

class OSDMapTest : public testing::Test {
  const static int num_osds = 6;
public:
  OSDMap osdmap;
  OSDMapMapping mapping;
  const uint64_t my_ec_pool = 1;
  const uint64_t my_rep_pool = 2;


  OSDMapTest() {}

  void set_up_map() {
    uuid_d fsid;
    osdmap.build_simple(g_ceph_context, 0, fsid, num_osds);
    OSDMap::Incremental pending_inc(osdmap.get_epoch() + 1);
    pending_inc.fsid = osdmap.get_fsid();
    entity_addr_t sample_addr;
    uuid_d sample_uuid;
    for (int i = 0; i < num_osds; ++i) {
      sample_uuid.generate_random();
      sample_addr.nonce = i;
      pending_inc.new_state[i] = CEPH_OSD_EXISTS | CEPH_OSD_NEW;
      pending_inc.new_up_client[i] = sample_addr;
      pending_inc.new_up_cluster[i] = sample_addr;
      pending_inc.new_hb_back_up[i] = sample_addr;
      pending_inc.new_hb_front_up[i] = sample_addr;
      pending_inc.new_weight[i] = CEPH_OSD_IN;
      pending_inc.new_uuid[i] = sample_uuid;
    }
    osdmap.apply_incremental(pending_inc);

    // Create an EC ruleset and a pool using it
    int r = osdmap.crush->add_simple_rule(
      "erasure", "default", "osd", "",
      "indep", pg_pool_t::TYPE_ERASURE,
      &cerr);

    OSDMap::Incremental new_pool_inc(osdmap.get_epoch() + 1);
    new_pool_inc.new_pool_max = osdmap.get_pool_max();
    new_pool_inc.fsid = osdmap.get_fsid();
    pg_pool_t empty;
    // make an ec pool
    uint64_t pool_id = ++new_pool_inc.new_pool_max;
    assert(pool_id == my_ec_pool);
    pg_pool_t *p = new_pool_inc.get_new_pool(pool_id, &empty);
    p->size = 3;
    p->set_pg_num(64);
    p->set_pgp_num(64);
    p->type = pg_pool_t::TYPE_ERASURE;
    p->crush_rule = r;
    new_pool_inc.new_pool_names[pool_id] = "ec";
    // and a replicated pool
    pool_id = ++new_pool_inc.new_pool_max;
    assert(pool_id == my_rep_pool);
    p = new_pool_inc.get_new_pool(pool_id, &empty);
    p->size = 3;
    p->set_pg_num(64);
    p->set_pgp_num(64);
    p->type = pg_pool_t::TYPE_REPLICATED;
    p->crush_rule = 0;
    p->set_flag(pg_pool_t::FLAG_HASHPSPOOL);
    new_pool_inc.new_pool_names[pool_id] = "reppool";
    osdmap.apply_incremental(new_pool_inc);
  }
  unsigned int get_num_osds() { return num_osds; }
  void get_crush(CrushWrapper& newcrush) {
    bufferlist bl;
    osdmap.crush->encode(bl, CEPH_FEATURES_SUPPORTED_DEFAULT);
    bufferlist::iterator p = bl.begin();
    newcrush.decode(p);
  }
  int crush_move(const string &name, const vector<string> &argvec) {
    map<string,string> loc;
    CrushWrapper::parse_loc_map(argvec, &loc);
    CrushWrapper newcrush;
    get_crush(newcrush);
    if (!newcrush.name_exists(name)) {
       return -ENOENT;
    }
    int id = newcrush.get_item_id(name);
    int err;
    if (!newcrush.check_item_loc(g_ceph_context, id, loc, (int *)NULL)) {
      if (id >= 0) {
        err = newcrush.create_or_move_item(g_ceph_context, id, 0, name, loc);
      } else {
        err = newcrush.move_bucket(g_ceph_context, id, loc);
      }
      if (err >= 0) {
        OSDMap::Incremental pending_inc(osdmap.get_epoch() + 1);
        pending_inc.crush.clear();
        newcrush.encode(pending_inc.crush, CEPH_FEATURES_SUPPORTED_DEFAULT);
        osdmap.apply_incremental(pending_inc);
        err = 0;
      }
    } else {
      // already there
      err = 0;
    }
    return err;
  }
  int crush_rule_create_replicated(const string &name,
                                   const string &root,
                                   const string &type) {
    if (osdmap.crush->rule_exists(name)) {
      return osdmap.crush->get_rule_id(name);
    }
    CrushWrapper newcrush;
    get_crush(newcrush);
    string device_class;
    stringstream ss;
    int ruleno = newcrush.add_simple_rule(
              name, root, type, device_class,
              "firstn", pg_pool_t::TYPE_REPLICATED, &ss);
    if (ruleno >= 0) {
      OSDMap::Incremental pending_inc(osdmap.get_epoch() + 1);
      pending_inc.crush.clear();
      newcrush.encode(pending_inc.crush, CEPH_FEATURES_SUPPORTED_DEFAULT);
      osdmap.apply_incremental(pending_inc);
    }
    return ruleno;
  }
  void test_mappings(int pool,
		     int num,
		     vector<int> *any,
		     vector<int> *first,
		     vector<int> *primary) {
    mapping.update(osdmap);
    for (int i=0; i<num; ++i) {
      vector<int> up, acting;
      int up_primary, acting_primary;
      pg_t pgid(i, pool);
      osdmap.pg_to_up_acting_osds(pgid,
				  &up, &up_primary, &acting, &acting_primary);
      for (unsigned j=0; j<acting.size(); ++j)
	(*any)[acting[j]]++;
      if (!acting.empty())
	(*first)[acting[0]]++;
      if (acting_primary >= 0)
	(*primary)[acting_primary]++;

      // compare to precalc mapping
      vector<int> up2, acting2;
      int up_primary2, acting_primary2;
      pgid = osdmap.raw_pg_to_pg(pgid);
      mapping.get(pgid, &up2, &up_primary2, &acting2, &acting_primary2);
      ASSERT_EQ(up, up2);
      ASSERT_EQ(up_primary, up_primary2);
      ASSERT_EQ(acting, acting2);
      ASSERT_EQ(acting_primary, acting_primary2);
    }
    cout << "any: " << *any << std::endl;;
    cout << "first: " << *first << std::endl;;
    cout << "primary: " << *primary << std::endl;;
  }
};


TEST_F(OSDMapTest, BZ_1715577) {
    //auto pool_id = 462; //repl
    //pg_t rawpg(172, pool_id); //repl
    OSDMap osdmap;
    bufferlist bl;
    std::string fn ("/home/ubuntu/ceph/build/osd_map_pre");
    cerr << ": osdmap file '" << fn << "'" << std::endl;
    int r = 0;
    std::string error;
    r = bl.read_file(fn.c_str(), &error);
    if (r == 0) {
      try {
        osdmap.decode(bl);
      }
      catch (const buffer::error &e) {
        cerr << ": error decoding osdmap '" << fn << "'" << std::endl;
      }
    } else {
      cerr << ": couldn't open " << fn << ": " << error << std::endl;
    }

    {
      vector<pair<int32_t,int32_t>> new_pg_upmap_items;
      const uint64_t pool_id = 462;
      //pg_t ec_pgid(370, pool_id);
      pg_t ec_pgid(359, pool_id);
      //pg_t ec_pgid = osdmap.raw_pg_to_pg(ec_pg);
      //pg_t pgid = osdmap.raw_pg_to_pg(rawpg); // repl
      vector<int> original_up;
      int original_up_primary;
      cout << ": pg " << "ec_pg: " << ec_pgid << " ec_pgid " << ec_pgid << std::endl;
      osdmap.pg_to_raw_up(ec_pgid, &original_up, &original_up_primary);
      //osdmap.pg_to_raw_up(pgid, &original_up, &original_up_primary);
      cout << ": original up " << original_up << std::endl;
      cout << ": original epoch " << osdmap.get_epoch() << std::endl;
      /*
      new_pg_upmap_items.push_back(make_pair(2176, 2182)); // should be a valid mapping
      new_pg_upmap_items.push_back(make_pair(2135, 2136)); // should be a valid mapping
      new_pg_upmap_items.push_back(make_pair(1202, 1210)); // should be a valid mapping
      */
      new_pg_upmap_items.push_back(make_pair(1657, 1658)); // should be a valid mapping
      new_pg_upmap_items.push_back(make_pair(1581, 1577)); // should be a valid mapping
      new_pg_upmap_items.push_back(make_pair(2145, 2151)); // should be a valid mapping
      new_pg_upmap_items.push_back(make_pair(2135, 2134)); // should be a valid mapping
      cout << ": new_pg_upmap_items " << new_pg_upmap_items << std::endl;
      OSDMap::Incremental pending_inc(osdmap.get_epoch() + 1);
      pending_inc.new_pg_upmap_items[ec_pgid] =
      //pending_inc.new_pg_upmap_items[pgid] =
      mempool::osdmap::vector<pair<int32_t,int32_t>>(
        new_pg_upmap_items.begin(), new_pg_upmap_items.end());
      osdmap.maybe_remove_pg_upmaps(g_ceph_context, osdmap, &pending_inc);
      osdmap.apply_incremental(pending_inc);

      //ASSERT_TRUE(osdmap.have_pg_upmaps(ec_pgid));
      vector<int> up;
      int up_primary;
      osdmap.pg_to_raw_up(ec_pgid, &up, &up_primary);
      //osdmap.pg_to_raw_up(pgid, &up, &up_primary);
      cout << ": new up " << up << std::endl;
      cout << ": new epoch " << osdmap.get_epoch() << std::endl;
      cout << ": osdmap " << osdmap << std::endl;
      ASSERT_NE(original_up, up);
      //ASSERT_TRUE(up.size() == 3);
      //ASSERT_TRUE(up[0] == 10);
    }
}

