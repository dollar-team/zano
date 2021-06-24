// Copyright (c) 2014-2021 Zano Project
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chaingen.h"
#include "escrow_wallet_tests.h"
#include "random_helper.h"
#include "chaingen_helpers.h"
#include "atomic_tests.h"

using namespace epee;
using namespace crypto;
using namespace currency;


isolate_auditable_and_proof::isolate_auditable_and_proof()
{
  REGISTER_CALLBACK_METHOD(isolate_auditable_and_proof, c1);
  REGISTER_CALLBACK_METHOD(isolate_auditable_and_proof, configure_core);
}

bool isolate_auditable_and_proof::generate(std::vector<test_event_entry>& events) const
{
  random_state_test_restorer::reset_random(0); // to make the test deterministic
  m_genesis_timestamp = 1450000000;
  test_core_time::adjust(m_genesis_timestamp);


  epee::debug::get_set_enable_assert(true, true);

  currency::account_base genesis_acc;
  genesis_acc.generate();
  m_mining_accunt.generate();
  m_mining_accunt.set_createtime(m_genesis_timestamp);


  block blk_0 = AUTO_VAL_INIT(blk_0);
  generator.construct_genesis_block(blk_0, genesis_acc, test_core_time::get_time());
  events.push_back(blk_0);
  DO_CALLBACK(events, "configure_core");
  REWIND_BLOCKS_N(events, blk_0r, blk_0, m_mining_accunt, CURRENCY_MINED_MONEY_UNLOCK_WINDOW + 5);

  DO_CALLBACK(events, "c1");
  epee::debug::get_set_enable_assert(true, false);
  return true;
}

bool isolate_auditable_and_proof::configure_core(currency::core& c, size_t ev_index, const std::vector<test_event_entry>& events)
{
  return true;
}
/************************************************************************/
/*                                                                      */
/************************************************************************/

bool isolate_auditable_and_proof::c1(currency::core& c, size_t ev_index, const std::vector<test_event_entry>& events)
{
  epee::debug::get_set_enable_assert(true, true);
  misc_utils::auto_scope_leave_caller scope_exit_handler = misc_utils::create_scope_leave_handler([&](){epee::debug::get_set_enable_assert(true, false); });

  LOG_PRINT_MAGENTA("Mining Address: " << currency::get_account_address_as_str(m_mining_accunt.get_public_address()), LOG_LEVEL_0);

  currency::account_base auditable_test; 
  auditable_test.generate(true);
  auditable_test.set_createtime(m_genesis_timestamp);
  LOG_PRINT_MAGENTA(": " << currency::get_account_address_as_str(auditable_test.get_public_address()), LOG_LEVEL_0); 
  std::shared_ptr<tools::wallet2> auditable_test_instance = init_playtime_test_wallet(events, c, auditable_test);


#define AMOUNT_TO_TRANSFER_LOCAL    (TESTS_DEFAULT_FEE*10)

  std::shared_ptr<tools::wallet2> miner_wlt = init_playtime_test_wallet(events, c, m_mining_accunt);
  miner_wlt->refresh();


  //create transaction that use TX_SERVICE_ATTACHMENT_ENCRYPT_BODY and TX_SERVICE_ATTACHMENT_ENCRYPT_BODY_ISOLATE_AUDITABLE
  {
    std::vector<extra_v> extra;
    std::vector<currency::attachment_v> attachments;
    vector<currency::tx_destination_entry> dsts;

    currency::tx_destination_entry de;
    de.addr.resize(1);
    //put into service attachment specially encrypted entry which will contain wrap address and network
    tx_service_attachment sa = AUTO_VAL_INIT(sa);
    sa.service_id = BC_WRAP_SERVICE_ID;
    sa.instruction = BC_WRAP_SERVICE_INSTRUCTION_ERC20;
    sa.flags = TX_SERVICE_ATTACHMENT_ENCRYPT_BODY | TX_SERVICE_ATTACHMENT_ENCRYPT_BODY_ISOLATE_AUDITABLE;
    sa.body = "0xAb5801a7D398351b8bE11C439e05C5B3259aeC9B";
    extra.push_back(sa);

    currency::account_public_address acc = AUTO_VAL_INIT(acc);
    de.addr.front() = auditable_test.get_public_address();
    currency::transaction tx;
    miner_wlt->transfer(dsts, 0, 0, miner_wlt->get_core_runtime_config().tx_default_fee, extra, attachments, tx);
  }

  bool r = mine_next_pow_blocks_in_playtime(m_mining_accunt.get_public_address(), c, CURRENCY_MINED_MONEY_UNLOCK_WINDOW);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_blocks_in_playtime failed");

  miner_wlt->refresh();
  auditable_test_instance->refresh();

  epee::json_rpc::error je;
  tools::wallet_rpc_server::connection_context ctx;
  tools::wallet_rpc_server miner_wlt_rpc(*auditable_test_instance);
  wallet_public::COMMAND_RPC_GET_RECENT_TXS_AND_INFO::request req = AUTO_VAL_INIT();
  wallet_public::COMMAND_RPC_GET_RECENT_TXS_AND_INFO::request res = AUTO_VAL_INIT();
  req.count = 100;
  req.offset = 0;
  miner_wlt_rpc->on_get_recent_txs_and_info(req, res, je, ctx);
  std::string ps = epee::serialization::store_t_to_json(res);

  return r;
}

