/*
 * Copyright (c) 2003-2008, John Wiegley.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of New Artisans LLC nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "binary.h"
#include "journal.h"
#include "session.h"

namespace ledger {

static unsigned long binary_magic_number = 0xFFEED765;
#ifdef DEBUG_ENABLED
static unsigned long format_version      = 0x00020701;
#else
static unsigned long format_version      = 0x00020700;
#endif

static account_t **	      accounts;
static account_t **	      accounts_next;
static unsigned int	      account_index;

static commodity_t::base_t ** base_commodities;
static commodity_t::base_t ** base_commodities_next;
static unsigned int	      base_commodity_index;

static commodity_t **	      commodities;
static commodity_t **	      commodities_next;
static unsigned int	      commodity_index;

extern char *		      bigints;
extern char *		      bigints_next;
extern unsigned int	      bigints_index;
extern unsigned int	      bigints_count;

bool journal_t::binary_parser_t::test(std::istream& in) const
{
  if (binary::read_number_nocheck<unsigned long>(in) == binary_magic_number &&
      binary::read_number_nocheck<unsigned long>(in) == format_version)
    return true;

  in.clear();
  in.seekg(0, std::ios::beg);
  return false;
}

namespace binary {
  unsigned int read_journal(std::istream& in,
			    const path&	  file,
			    journal_t&	  journal,
			    account_t *	  master);
}

unsigned int journal_t::binary_parser_t::parse(std::istream& in,
					       session_t&    session,
					       journal_t&    journal,
					       account_t *   master,
					       const path *  original_file)
{
  return journal.read(in, original_file ? *original_file : "", master);
}

namespace binary {

void read_bool(std::istream& in, bool& num)
{
  read_guard(in, 0x2005);
  unsigned char val;
  in.read(reinterpret_cast<char *>(&val), sizeof(val));
  num = val == 1;
  read_guard(in, 0x2006);
}

void read_bool(const char *& data, bool& num)
{
  read_guard(data, 0x2005);
  const unsigned char val = *reinterpret_cast<const unsigned char *>(data);
  data += sizeof(unsigned char);
  num = val == 1;
  read_guard(data, 0x2006);
}

void read_string(std::istream& in, string& str)
{
  read_guard(in, 0x3001);

  unsigned char len;
  read_number_nocheck(in, len);
  if (len == 0xff) {
    unsigned short slen;
    read_number_nocheck(in, slen);
    char * buf = new char[slen + 1];
    in.read(buf, slen);
    buf[slen] = '\0';
    str = buf;
    checked_array_delete(buf);
  }
  else if (len) {
    char buf[256];
    in.read(buf, len);
    buf[len] = '\0';
    str = buf;
  } else {
    str = "";
  }

  read_guard(in, 0x3002);
}

void read_string(const char *& data, string& str)
{
  read_guard(data, 0x3001);

  unsigned char len;
  read_number_nocheck(data, len);
  if (len == 0xff) {
    unsigned short slen;
    read_number_nocheck(data, slen);
    str = string(data, slen);
    data += slen;
  }
  else if (len) {
    str = string(data, len);
    data += len;
  }
  else {
    str = "";
  }

  read_guard(data, 0x3002);
}

void read_string(const char *& data, string * str)
{
  read_guard(data, 0x3001);

  unsigned char len;
  read_number_nocheck(data, len);
  if (len == 0xff) {
    unsigned short slen;
    read_number_nocheck(data, slen);
    new(str) string(data, slen);
    data += slen;
  }
  else if (len) {
    new(str) string(data, len);
    data += len;
  }
  else {
    new(str) string("");
  }

  read_guard(data, 0x3002);
}

void read_string(std::istream& in, optional<string>& str)
{
  if (read_bool(in)) {
    string temp;
    read_string(in, temp);
    str = temp;
  } else {
    str = none;
  }
}

void read_string(const char *& data, optional<string>& str)
{
  if (read_bool(data)) {
    string temp;
    read_string(data, temp);
    str = temp;
  } else {
    str = none;
  }
}


void write_bool(std::ostream& out, bool num)
{
  write_guard(out, 0x2005);
  unsigned char val = num ? 1 : 0;
  out.write(reinterpret_cast<char *>(&val), sizeof(val));
  write_guard(out, 0x2006);
}

void write_string(std::ostream& out, const string& str)
{
  write_guard(out, 0x3001);

  unsigned long len = str.length();
  if (len > 255) {
    assert(len < 65536);
    write_number_nocheck<unsigned char>(out, 0xff);
    write_number_nocheck<unsigned short>(out, len);
  } else {
    write_number_nocheck<unsigned char>(out, len);
  }

  if (len)
    out.write(str.c_str(), len);

  write_guard(out, 0x3002);
}

void write_string(std::ostream& out, const optional<string>& str)
{
  if (str) {
    write_bool(out, true);
    write_string(out, *str);
  } else {
    write_bool(out, false);
  }
}

inline void read_amount(const char *& data, amount_t& amt)
{
  commodity_t::ident_t ident;
  read_long(data, ident);
  if (ident == 0xffffffff)
    amt.commodity_ = NULL;
  else if (ident == 0)
    amt.commodity_ = amount_t::current_pool->null_commodity;
  else
    amt.commodity_ = commodities[ident - 1];

  amt.read(data);
}

inline void read_value(const char *& data, value_t& val)
{
  switch (static_cast<value_t::type_t>(read_long<int>(data))) {
  case value_t::BOOLEAN:
    val.set_boolean(read_bool(data));
    break;
  case value_t::INTEGER:
    val.set_long(read_number<unsigned long>(data));
    break;
  case value_t::DATETIME:
    // jww (2008-04-22): I need to record and read a datetime_t directly
    //val.set_datetime(read_long<unsigned long>(data));
    break;
  case value_t::AMOUNT: {
    amount_t temp;
    read_amount(data, temp);
    val.set_amount(temp);
    break;
  }

  //case value_t::BALANCE:
  //case value_t::BALANCE_PAIR:
  default:
    assert(false);
    break;
  }
}

inline void read_mask(const char *& data, mask_t& mask)
{
  bool exclude;
  read_number(data, exclude);
  string pattern;
  read_string(data, pattern);

  mask = mask_t(pattern);
  mask.exclude = exclude;
}

inline void read_xact(const char *& data, xact_t * xact)
{
  read_number(data, xact->_date);
  read_number(data, xact->_date_eff);
  xact->account = accounts[read_long<account_t::ident_t>(data) - 1];

  unsigned char flag = read_number<unsigned char>(data);
  if (flag == 0) {
    read_amount(data, xact->amount);
  }
  else if (flag == 1) {
    read_amount(data, xact->amount);
    string str;
    read_string(data, str);
    xact->amount_expr->set_text(str);
  }
  else {
    xact->amount_expr->read(data);
  }

  if (read_bool(data)) {
    xact->cost = amount_t();
    read_amount(data, *xact->cost);

    xact->cost_expr = expr_t();
    xact->cost_expr->read(data);
  } else {
    xact->cost = none;
  }

  read_number(data, xact->state);
  xact->set_flags(read_number<xact_t::flags_t>(data));
  xact->add_flags(XACT_BULK_ALLOC);
  read_string(data, xact->note);

  xact->beg_pos = read_long<unsigned long>(data);
  read_long(data, xact->beg_line);
  xact->end_pos = read_long<unsigned long>(data);
  read_long(data, xact->end_line);

  xact->data = NULL;

#if 0
  if (xact->amount_expr)
    expr_t::compute_amount(xact->amount_expr.get(), xact->amount, xact);
#endif
}

inline void read_entry_base(const char *& data, entry_base_t * entry,
			    xact_t *& xact_pool, bool& finalize)
{
  read_long(data, entry->src_idx);
  entry->beg_pos = read_long<unsigned long>(data);
  read_long(data, entry->beg_line);
  entry->end_pos = read_long<unsigned long>(data);
  read_long(data, entry->end_line);

  bool ignore_calculated = read_bool(data);

  for (unsigned long i = 0, count = read_long<unsigned long>(data);
       i < count;
       i++) {
    new(xact_pool) xact_t;
    read_xact(data, xact_pool);
    if (ignore_calculated && xact_pool->has_flags(XACT_CALCULATED))
      finalize = true;
    entry->add_xact(xact_pool++);
  }
}

inline void read_entry(const char *& data, entry_t * entry,
		       xact_t *& xact_pool, bool& finalize)
{
  read_entry_base(data, entry, xact_pool, finalize);
  read_number(data, entry->_date);
  read_number(data, entry->_date_eff);
  read_string(data, entry->code);
  read_string(data, entry->payee);
}

inline void read_auto_entry(const char *& data, auto_entry_t * entry,
			    xact_t *& xact_pool)
{
  bool ignore;
  read_entry_base(data, entry, xact_pool, ignore);

  expr_t expr;
  expr.read(data);
  entry->predicate = item_predicate<xact_t>(expr);
}

inline void read_period_entry(const char *& data, period_entry_t * entry,
			      xact_t *& xact_pool, bool& finalize)
{
  read_entry_base(data, entry, xact_pool, finalize);
  read_string(data, &entry->period_string);
  std::istringstream stream(entry->period_string);
  entry->period.parse(stream);
}

inline commodity_t::base_t * read_commodity_base(const char *& data)
{
  string str;
  
  read_string(data, str);

  std::auto_ptr<commodity_t::base_t> commodity(new commodity_t::base_t(str));

  read_string(data, str);
  if (! str.empty())
    commodity->name = str;

  read_string(data, str);
  if (! str.empty())
    commodity->note = str;

  read_number(data, commodity->precision);
  unsigned long flags;
  read_number(data, flags);
  commodity->set_flags(flags);

  return *base_commodities_next++ = commodity.release();
}

inline void read_commodity_base_extra(const char *& data,
				      commodity_t::ident_t ident)
{
  commodity_t::base_t * commodity = base_commodities[ident];

  bool read_history = false;
  for (unsigned long i = 0, count = read_long<unsigned long>(data);
       i < count;
       i++) {
    datetime_t when;
    read_number(data, when);
    amount_t amt;
    read_amount(data, amt);

    // Upon insertion, amt will be copied, which will cause the amount
    // to be duplicated (and thus not lost when the journal's
    // item_pool is deleted).
    if (! commodity->history)
      commodity->history = commodity_t::history_t();
    commodity->history->prices.insert(commodity_t::base_t::history_pair(when, amt));

    read_history = true;
  }
  if (read_history)
    read_number(data, commodity->history->last_lookup);

  if (read_bool(data)) {
    amount_t amt;
    read_amount(data, amt);
    commodity->smaller = amount_t(amt);
  }

  if (read_bool(data)) {
    amount_t amt;
    read_amount(data, amt);
    commodity->larger = amount_t(amt);
  }
}

inline commodity_t * read_commodity(const char *& data)
{
  commodity_t::base_t * base =
    base_commodities[read_long<commodity_t::ident_t>(data) - 1];

  commodity_t * commodity =
    new commodity_t(amount_t::current_pool,
		    shared_ptr<commodity_t::base_t>(base));

  *commodities_next++ = commodity;

  string str;
  read_string(data, str);
  if (! str.empty())
    commodity->qualified_symbol = str;
  commodity->annotated = false;

  return commodity;
}

inline commodity_t * read_commodity_annotated(const char *& data)
{
  commodity_t * commodity = 
    commodities[read_long<commodity_t::ident_t>(data) - 1];

  annotation_t details;

  string str;
  read_string(data, str);

  // This read-and-then-assign causes a new amount to be allocated which does
  // not live within the bulk allocation pool, since that pool will be deleted
  // *before* the commodities are destroyed.
  amount_t amt;
  read_amount(data, amt);
  details.price = amt;

#if 0
  // jww (2008-04-22): These are optional members!
  read_number(data, details.date);
  read_string(data, details.tag);
#endif

  annotated_commodity_t * ann_comm =
    new annotated_commodity_t(commodity, details);
  *commodities_next++ = ann_comm;

  if (! str.empty())
    ann_comm->qualified_symbol = str;

  return ann_comm;
}

inline
account_t * read_account(const char *& data, journal_t& journal,
			 account_t * master = NULL)
{
  account_t * acct = new account_t(NULL);
  *accounts_next++ = acct;

  account_t::ident_t id;
  read_long(data, id);	// parent id
  if (id == 0xffffffff)
    acct->parent = NULL;
  else
    acct->parent = accounts[id - 1];

  read_string(data, acct->name);
  read_string(data, acct->note);
  read_number(data, acct->depth);

  // If all of the subaccounts will be added to a different master
  // account, throw away what we've learned about the recorded
  // journal's own master account.

  if (master && acct != master) {
    checked_delete(acct);
    acct = master;
  }

  for (account_t::ident_t i = 0,
	 count = read_long<account_t::ident_t>(data);
       i < count;
       i++) {
    account_t * child = read_account(data, journal);
    child->parent = acct;
    assert(acct != child);
    acct->add_account(child);
  }

  return acct;
}

void write_amount(std::ostream& out, const amount_t& amt)
{
  if (amt.commodity_)
    write_long(out, amt.commodity_->ident);
  else
    write_long<commodity_t::ident_t>(out, 0xffffffff);

  amt.write(out);
}

void write_value(std::ostream& out, const value_t& val)
{
  write_long(out, static_cast<int>(val.type()));

  switch (val.type()) {
  case value_t::BOOLEAN:
    write_bool(out, val.as_boolean());
    break;
  case value_t::INTEGER:
    write_long(out, val.as_long());
    break;
  case value_t::DATETIME:
    write_number(out,val.as_datetime());
    break;
  case value_t::AMOUNT:
    write_amount(out, val.as_amount());
    break;

  //case value_t::BALANCE:
  //case value_t::BALANCE_PAIR:
  default:
    throw new error("Cannot write a balance to the binary cache");
  }
}

void write_mask(std::ostream& out, mask_t& mask)
{
  write_number(out, mask.exclude);
  write_string(out, mask.expr.str());
}

void write_xact(std::ostream& out, xact_t * xact,
		       bool ignore_calculated)
{
  write_number(out, xact->_date);
  write_number(out, xact->_date_eff);
  write_long(out, xact->account->ident);

  if (ignore_calculated && xact->has_flags(XACT_CALCULATED)) {
    write_number<unsigned char>(out, 0);
    write_amount(out, amount_t());
  }
  else if (xact->amount_expr) {
    write_number<unsigned char>(out, 2);
    xact->amount_expr->write(out);
  }
  else if (! xact->amount_expr->text().empty()) {
    write_number<unsigned char>(out, 1);
    write_amount(out, xact->amount);
    write_string(out, xact->amount_expr->text());
  }
  else {
    write_number<unsigned char>(out, 0);
    write_amount(out, xact->amount);
  }

  if (xact->cost &&
      (! (ignore_calculated && xact->has_flags(XACT_CALCULATED)))) {
    write_bool(out, true);
    write_amount(out, *xact->cost);
    xact->cost_expr->write(out);
  } else {
    write_bool(out, false);
  }

  write_number(out, xact->state);
  write_number(out, xact->flags());
  write_string(out, xact->note);

  write_long(out, xact->beg_pos);
  write_long(out, xact->beg_line);
  write_long(out, xact->end_pos);
  write_long(out, xact->end_line);
}

void write_entry_base(std::ostream& out, entry_base_t * entry)
{
  write_long(out, entry->src_idx);
  write_long(out, entry->beg_pos);
  write_long(out, entry->beg_line);
  write_long(out, entry->end_pos);
  write_long(out, entry->end_line);

  bool ignore_calculated = false;
  for (xacts_list::const_iterator i = entry->xacts.begin();
       i != entry->xacts.end();
       i++)
    if ((*i)->amount_expr) {
      ignore_calculated = true;
      break;
    }

  write_bool(out, ignore_calculated);

  write_long(out, entry->xacts.size());
  for (xacts_list::const_iterator i = entry->xacts.begin();
       i != entry->xacts.end();
       i++)
    write_xact(out, *i, ignore_calculated);
}

void write_entry(std::ostream& out, entry_t * entry)
{
  write_entry_base(out, entry);
  write_number(out, entry->_date);
  write_number(out, entry->_date_eff);
  write_string(out, entry->code);
  write_string(out, entry->payee);
}

void write_auto_entry(std::ostream& out, auto_entry_t * entry)
{
  write_entry_base(out, entry);
  entry->predicate.predicate.write(out);
}

void write_period_entry(std::ostream& out, period_entry_t * entry)
{
  write_entry_base(out, entry);
  write_string(out, entry->period_string);
}

void write_commodity_base(std::ostream& out, commodity_t::base_t * commodity)
{
  // jww (2008-04-22): Not using this anymore?
  //commodity->ident = ++base_commodity_index;

  write_string(out, commodity->symbol);
  // jww (2008-04-22): What to do with optional members?
  write_string(out, *commodity->name);
  write_string(out, *commodity->note);
  write_number(out, commodity->precision);
  write_number(out, commodity->flags());
}

void write_commodity_base_extra(std::ostream& out,
				commodity_t::base_t * commodity)
{
#if 0
  // jww (2008-04-22): What did bogus_time used to do?
  if (commodity->history && commodity->history->bogus_time)
    commodity->remove_price(commodity->history->bogus_time);
#endif

  if (! commodity->history) {
    write_long<unsigned long>(out, 0);
  } else {
    write_long<unsigned long>(out, commodity->history->prices.size());
    for (commodity_t::history_map::const_iterator
	   i = commodity->history->prices.begin();
	 i != commodity->history->prices.end();
	 i++) {
      write_number(out, (*i).first);
      write_amount(out, (*i).second);
    }
    write_number(out, commodity->history->last_lookup);
  }

  if (commodity->smaller) {
    write_bool(out, true);
    write_amount(out, *commodity->smaller);
  } else {
    write_bool(out, false);
  }

  if (commodity->larger) {
    write_bool(out, true);
    write_amount(out, *commodity->larger);
  } else {
    write_bool(out, false);
  }
}

void write_commodity(std::ostream& out, commodity_t * commodity)
{
  commodity->ident = ++commodity_index;

  // jww (2008-04-22): Is this used anymore?
  //write_long(out, commodity->base->ident);
  // jww (2008-04-22): Optional!
  write_string(out, *commodity->qualified_symbol);
}

void write_commodity_annotated(std::ostream& out,
				      commodity_t * commodity)
{
  commodity->ident = ++commodity_index;

  // jww (2008-04-22): No longer needed?
  //write_long(out, commodity->base->ident);
  // jww (2008-04-22): Optional!
  write_string(out, *commodity->qualified_symbol);

  annotated_commodity_t * ann_comm =
    static_cast<annotated_commodity_t *>(commodity);

  // jww (2008-04-22): No longer needed?
  //write_long(out, ann_comm->base->ident);
  // jww (2008-04-22): Make a write_annotation_details function; and optional!
  write_amount(out, *ann_comm->details.price);
  write_number(out, *ann_comm->details.date);
  write_string(out, *ann_comm->details.tag);
}

static inline account_t::ident_t count_accounts(account_t * account)
{
  account_t::ident_t count = 1;

  for (accounts_map::iterator i = account->accounts.begin();
       i != account->accounts.end();
       i++)
    count += count_accounts((*i).second);

  return count;
}

void write_account(std::ostream& out, account_t * account)
{
  account->ident = ++account_index;

  if (account->parent)
    write_long(out, account->parent->ident);
  else
    write_long<account_t::ident_t>(out, 0xffffffff);

  write_string(out, account->name);
  write_string(out, account->note);
  write_number(out, account->depth);

  write_long<account_t::ident_t>(out, account->accounts.size());
  for (accounts_map::iterator i = account->accounts.begin();
       i != account->accounts.end();
       i++)
    write_account(out, (*i).second);
}

} // namespace binary

unsigned int journal_t::read(std::istream& in,
			     const path&   file,
			     account_t *   master)
{
  using namespace binary;

  account_index	       = 
  base_commodity_index = 
  commodity_index      = 0;

  // Read in the files that participated in this journal, so that they
  // can be checked for changes on reading.

  if (! file.empty()) {
    for (unsigned short i = 0,
	   count = read_number<unsigned short>(in);
	 i < count;
	 i++) {
      path pathname = read_string(in);
      std::time_t old_mtime;
      read_number(in, old_mtime);
      struct stat info;
      // jww (2008-04-22): can this be done differently now?
      stat(pathname.string().c_str(), &info);
      if (std::difftime(info.st_mtime, old_mtime) > 0)
	return 0;

      sources.push_back(pathname);
    }

    // Make sure that the cache uses the same price database,
    // otherwise it means that LEDGER_PRICE_DB has been changed, and
    // we should ignore this cache file.
    if (read_bool(in)) {
      string pathname;
      read_string(in, pathname);
      if (! price_db ||
	  price_db->string() != std::string(pathname))
	return 0;
    }
  }

  // Read all of the data in at once, so that we're just dealing with
  // a big data buffer.

  unsigned long data_size = read_number<unsigned long>(in);

  char * data_pool = new char[data_size];
  in.read(data_pool, data_size);

  // Read in the accounts

  const char * data = data_pool;

  account_t::ident_t a_count = read_long<account_t::ident_t>(data);
  accounts = accounts_next = new account_t *[a_count];

  // jww (2008-07-29): Does this still apply?
  assert(owner->master);
  checked_delete(owner->master);
  owner->master = read_account(data, *this, master);

  if (read_bool(data))
    basket = accounts[read_long<account_t::ident_t>(data) - 1];

  // Allocate the memory needed for the entries and xacts in
  // one large block, which is then chopped up and custom constructed
  // as necessary.

  unsigned long count        = read_long<unsigned long>(data);
  unsigned long auto_count   = read_long<unsigned long>(data);
  unsigned long period_count = read_long<unsigned long>(data);
  unsigned long xact_count   = read_number<unsigned long>(data);
  unsigned long bigint_count = read_number<unsigned long>(data);

  std::size_t pool_size = (sizeof(entry_t) * count +
			   sizeof(xact_t) * xact_count +
			   amount_t::sizeof_bigint_t() * bigint_count);

  char * item_pool = new char[pool_size];

  item_pool	= item_pool;
  item_pool_end = item_pool + pool_size;

  entry_t * entry_pool = reinterpret_cast<entry_t *>(item_pool);
  xact_t * xact_pool   = reinterpret_cast<xact_t *>(item_pool +
						    (sizeof(entry_t) * count));
  bigints_index = 0;
  bigints = bigints_next = (item_pool + sizeof(entry_t) * count +
			    sizeof(xact_t) * xact_count);

  // Read in the base commodities and then derived commodities

  commodity_t::ident_t bc_count = read_long<commodity_t::ident_t>(data);
  base_commodities = base_commodities_next = new commodity_t::base_t *[bc_count];

  for (commodity_t::ident_t i = 0; i < bc_count; i++) {
#if 0
    commodity_t::base_t * base = read_commodity_base(data);

    // jww (2008-04-22): How does the pool get created here?
    amount_t::current_pool->commodities.push_back(commodity);

    // jww (2008-04-22): What about this logic here?
    if (! result.second) {
      base_commodities_map::iterator c =
	commodity_t::base_t::commodities.find(commodity->symbol);

      // It's possible the user might have used a commodity in a value
      // expression passed to an option, we'll just override the
      // flags, but keep the commodity pointer intact.
      if (c == commodity_t::base_t::commodities.end())
	throw new error(string("Failed to read base commodity from cache: ") +
			commodity->symbol);

      (*c).second->name	     = commodity->name;
      (*c).second->note	     = commodity->note;
      (*c).second->precision = commodity->precision;
      (*c).second->flags     = commodity->flags;
      if ((*c).second->smaller)
	checked_delete((*c).second->smaller);
      (*c).second->smaller   = commodity->smaller;
      if ((*c).second->larger)
	checked_delete((*c).second->larger);
      (*c).second->larger    = commodity->larger;

      *(base_commodities_next - 1) = (*c).second;
      checked_delete(commodity);
    }
#endif
  }

  commodity_t::ident_t c_count  = read_long<commodity_t::ident_t>(data);
  commodities = commodities_next = new commodity_t *[c_count];

  for (commodity_t::ident_t i = 0; i < c_count; i++) {
    commodity_t * commodity;
    string	  mapping_key;

    if (! read_bool(data)) {
      commodity	  = read_commodity(data);
      mapping_key = commodity->base->symbol;
    } else {
      read_string(data, mapping_key);
      commodity = read_commodity_annotated(data);
    }

    // jww (2008-04-22): What do I do with mapping_key here?
    amount_t::current_pool->commodities.push_back(commodity);
#if 0
    // jww (2008-04-22): What about the error case?
    if (! result.second) {
      commodities_map::iterator c =
	commodity_t::commodities.find(mapping_key);
      if (c == commodity_t::commodities.end())
	throw new error(string("Failed to read commodity from cache: ") +
			commodity->symbol());

      *(commodities_next - 1) = (*c).second;
      checked_delete(commodity);
    }
#endif
  }

  for (commodity_t::ident_t i = 0; i < bc_count; i++)
    read_commodity_base_extra(data, i);

  commodity_t::ident_t ident;
  read_long(data, ident);
  if (ident == 0xffffffff || ident == 0)
    amount_t::current_pool->default_commodity = NULL;
  else
    amount_t::current_pool->default_commodity = commodities[ident - 1];

  // Read in the entries and xacts

  for (unsigned long i = 0; i < count; i++) {
    new(entry_pool) entry_t;
    bool finalize = false;
    read_entry(data, entry_pool, xact_pool, finalize);
    entry_pool->journal = this;
    if (finalize && ! entry_pool->finalize())
      continue;
    entries.push_back(entry_pool++);
  }

  for (unsigned long i = 0; i < auto_count; i++) {
    auto_entry_t * auto_entry = new auto_entry_t;
    read_auto_entry(data, auto_entry, xact_pool);
    auto_entry->journal = this;
    auto_entries.push_back(auto_entry);
  }

  for (unsigned long i = 0; i < period_count; i++) {
    period_entry_t * period_entry = new period_entry_t;
    bool finalize = false;
    read_period_entry(data, period_entry, xact_pool, finalize);
    period_entry->journal = this;
    if (finalize && ! period_entry->finalize())
      continue;
    period_entries.push_back(period_entry);
  }

  // Clean up and return the number of entries read

  checked_array_delete(accounts);
  checked_array_delete(commodities);
  checked_array_delete(data_pool);

  VERIFY(valid());

  return count;
}

void journal_t::write(std::ostream& out)
{
  using namespace binary;

  account_index	       = 
  base_commodity_index = 
  commodity_index      = 0;

  write_number_nocheck(out, binary_magic_number);
  write_number_nocheck(out, format_version);

  // Write out the files that participated in this journal, so that
  // they can be checked for changes on reading.

  if (sources.empty()) {
    write_number<unsigned short>(out, 0);
  } else {
    write_number<unsigned short>(out, sources.size());
    for (paths_list::const_iterator i = sources.begin();
	 i != sources.end();
	 i++) {
      write_string(out, (*i).string());
      struct stat info;
      stat((*i).string().c_str(), &info);
      write_number(out, std::time_t(info.st_mtime));
    }

    // Write out the price database that relates to this data file, so
    // that if it ever changes the cache can be invalidated.
    if (price_db) {
      write_bool(out, true);
      write_string(out, price_db->string());
    } else {
      write_bool(out, false);
    }
  }

  ostream_pos_type data_val = out.tellp();
  write_number<unsigned long>(out, 0);

  // Write out the accounts

  write_long<account_t::ident_t>(out, count_accounts(master));
  write_account(out, master);

  if (basket) {
    write_bool(out, true);
    write_long(out, basket->ident);
  } else {
    write_bool(out, false);
  }

  // Write out the number of entries, xacts, and amounts

  write_long<unsigned long>(out, entries.size());
  write_long<unsigned long>(out, auto_entries.size());
  write_long<unsigned long>(out, period_entries.size());

  ostream_pos_type xacts_val = out.tellp();
  write_number<unsigned long>(out, 0);

  ostream_pos_type bigints_val = out.tellp();
  write_number<unsigned long>(out, 0);

  bigints_count = 0;

  // Write out the commodities
  // jww (2008-04-22): This whole section needs to be reworked

#if 0
  write_long<commodity_t::ident_t>(out, amount_t::current_pool->commodities.size());

  for (base_commodities_map::const_iterator i =
	 commodity_t::base_t::commodities.begin();
       i != commodity_t::base_t::commodities.end();
       i++)
    write_commodity_base(out, (*i).second);

  write_long<commodity_t::ident_t>
    (out, commodity_t::commodities.size());

  for (commodities_map::const_iterator i = commodity_t::commodities.begin();
       i != commodity_t::commodities.end();
       i++) {
    if (! (*i).second->annotated) {
      write_bool(out, false);
      write_commodity(out, (*i).second);
    }
  }

  for (commodities_map::const_iterator i = commodity_t::commodities.begin();
       i != commodity_t::commodities.end();
       i++) {
    if ((*i).second->annotated) {
      write_bool(out, true);
      write_string(out, (*i).first); // the mapping key
      write_commodity_annotated(out, (*i).second);
    }
  }

  // Write out the history and smaller/larger convertible links after
  // both the base and the main commodities have been written, since
  // the amounts in both will refer to the mains.

  for (base_commodities_map::const_iterator i =
	 commodity_t::base_t::commodities.begin();
       i != commodity_t::base_t::commodities.end();
       i++)
    write_commodity_base_extra(out, (*i).second);

  if (commodity_t::default_commodity)
    write_long(out, commodity_t::default_commodity->ident);
  else
    write_long<commodity_t::ident_t>(out, 0xffffffff);
#endif

  // Write out the entries and xacts

  unsigned long xact_count = 0;

  for (entries_list::const_iterator i = entries.begin();
       i != entries.end();
       i++) {
    write_entry(out, *i);
    xact_count += (*i)->xacts.size();
  }

  for (auto_entries_list::const_iterator i = auto_entries.begin();
       i != auto_entries.end();
       i++) {
    write_auto_entry(out, *i);
    xact_count += (*i)->xacts.size();
  }

  for (period_entries_list::const_iterator i = period_entries.begin();
       i != period_entries.end();
       i++) {
    write_period_entry(out, *i);
    xact_count += (*i)->xacts.size();
  }

  // Back-patch the count for amounts

  unsigned long data_size = (static_cast<unsigned long>(out.tellp()) -
			     static_cast<unsigned long>(data_val) -
			     sizeof(unsigned long));
  out.seekp(data_val);
  write_number<unsigned long>(out, data_size);
  out.seekp(xacts_val);
  write_number<unsigned long>(out, xact_count);
  out.seekp(bigints_val);
  write_number<unsigned long>(out, bigints_count);
}

} // namespace ledger
