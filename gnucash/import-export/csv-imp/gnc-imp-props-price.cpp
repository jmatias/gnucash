/********************************************************************\
 * gnc-imp-props-price.cpp - encapsulate price properties for use   *
 *                           in the csv importer                    *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
 *                                                                  *
\********************************************************************/

#include <glib.h>
#include <glib/gi18n.h>

extern "C" {
#include <platform.h>
#if PLATFORM(WINDOWS)
#include <windows.h>
#endif

#include "engine-helpers.h"
#include "gnc-ui-util.h"
}

#include <exception>
#include <map>
#include <string>
#include <boost/locale.hpp>
#include <boost/regex.hpp>
#include <boost/regex/icu.hpp>
#include <gnc-locale-utils.hpp>
#include "gnc-imp-props-price.hpp"

namespace bl = boost::locale;

G_GNUC_UNUSED static QofLogModule log_module = GNC_MOD_IMPORT;

/* This map contains a set of strings representing the different column types. */
std::map<GncPricePropType, const char*> gnc_price_col_type_strs = {
        { GncPricePropType::NONE, N_("None") },
        { GncPricePropType::DATE, N_("Date") },
        { GncPricePropType::AMOUNT, N_("Amount") },
        { GncPricePropType::FROM_SYMBOL, N_("From Symbol") },
        { GncPricePropType::FROM_NAMESPACE, N_("From Namespace") },
        { GncPricePropType::TO_CURRENCY, N_("Currency To") },
};

/** Convert str into a GncNumeric using the user-specified (import) currency format.
 * @param str The string to be parsed
 * @param currency_format The currency format to use.
 * @return a GncNumeric
 * @exception May throw std::invalid argument if string can't be parsed properly
 */
GncNumeric parse_amount_price (const std::string &str, int currency_format)
{
    /* If a cell is empty or just spaces return invalid amount */
    if(!boost::regex_search(str, boost::regex("[0-9]")))
        throw std::invalid_argument (_("Value doesn't appear to contain a valid number."));

    auto expr = boost::make_u32regex("[[:Sc:]]");
    std::string str_no_symbols = boost::u32regex_replace(str, expr, "");

    /* Convert based on user chosen currency format */
    gnc_numeric val = gnc_numeric_zero();
    char *endptr;
    switch (currency_format)
    {
    case 0:
        /* Currency locale */
        if (!(xaccParseAmountPosSign (str_no_symbols.c_str(), TRUE, &val, &endptr, TRUE)))
            throw std::invalid_argument (_("Value can't be parsed into a number using the selected currency format."));
        break;
    case 1:
        /* Currency decimal period */
        if (!(xaccParseAmountExtended (str_no_symbols.c_str(), TRUE, '-', '.', ',', "$+", &val, &endptr)))
            throw std::invalid_argument (_("Value can't be parsed into a number using the selected currency format."));
        break;
    case 2:
        /* Currency decimal comma */
        if (!(xaccParseAmountExtended (str_no_symbols.c_str(), TRUE, '-', ',', '.', "$+", &val, &endptr)))
            throw std::invalid_argument (_("Value can't be parsed into a number using the selected currency format."));
        break;
    }

    return GncNumeric(val);
}

/** Convert the combination of symbol_str and namespace_str into a gnc_commodity.
 * @param symbol_str The symbol string to be parsed
 * @param namespace_str The Namespace for this commodity
 * @return a gnc_commodity
 * @exception May throw std::invalid argument if string can't be parsed properly
 */
gnc_commodity* parse_commodity_price_comm (const std::string& symbol_str, const std::string& namespace_str)
{
    if (symbol_str.empty())
        return nullptr;

    auto table = gnc_commodity_table_get_table (gnc_get_current_book());
    gnc_commodity* comm = nullptr;

    /* First try commodity as a unique name, used in loading settings, returns null if not found */
    comm = gnc_commodity_table_lookup_unique (table, symbol_str.c_str());

    /* Now lookup with namespace and symbol */
    if (!comm)
    {
        comm = gnc_commodity_table_lookup (table,
                    namespace_str.c_str(), symbol_str.c_str());
    }

    if (!comm)
        throw std::invalid_argument (_("Value can't be parsed into a valid commodity."));
    else
        return comm;
}

/** Check for a valid namespace.
 * @param namespace_str The string to be parsed
 * @return a bool
 * @exception May throw std::invalid argument if string can't be parsed properly
 */
bool parse_namespace (const std::string& namespace_str)
{
    if (namespace_str.empty())
        return false;

    auto table = gnc_commodity_table_get_table (gnc_get_current_book());

    if (gnc_commodity_table_has_namespace (table, namespace_str.c_str()))
        return true;
    else
        throw std::invalid_argument (_("Value can't be parsed into a valid namespace."));

    return false;
}

void GncImportPrice::set (GncPricePropType prop_type, const std::string& value, bool enable_test_empty)
{
    try
    {
        // Drop any existing error for the prop_type we're about to set
        m_errors.erase(prop_type);

        // conditional test for empty values
        if (value.empty() && enable_test_empty)
            throw std::invalid_argument (_("Column value can not be empty."));

        gnc_commodity *comm = nullptr;
        switch (prop_type)
        {
            case GncPricePropType::DATE:
                m_date = boost::none;
                m_date = GncDate(value, GncDate::c_formats[m_date_format].m_fmt); // Throws if parsing fails
                break;

            case GncPricePropType::AMOUNT:
                m_amount = boost::none;
                m_amount = parse_amount_price (value, m_currency_format); // Throws if parsing fails
                break;

            case GncPricePropType::FROM_SYMBOL:
                m_from_symbol = boost::none;

                if (value.empty())
                    throw std::invalid_argument (_("'From Symbol' can not be empty."));
                else
                    m_from_symbol = value;

                if (m_from_namespace)
                {
                    comm = parse_commodity_price_comm (value, *m_from_namespace); // Throws if parsing fails
                    if (comm)
                    {
                        if (m_to_currency == comm)
                                throw std::invalid_argument (_("'Commodity From' can not be the same as 'Currency To'."));
                        m_from_commodity = comm;
                    }
                }
                break;

            case GncPricePropType::FROM_NAMESPACE:
                m_from_namespace = boost::none;

                if (value.empty())
                    throw std::invalid_argument (_("'From Namespace' can not be empty."));

                if (parse_namespace (value)) // Throws if parsing fails
                {
                    m_from_namespace = value;

                    if (m_from_symbol)
                    {
                        comm = parse_commodity_price_comm (*m_from_symbol, *m_from_namespace); // Throws if parsing fails
                        if (comm)
                        {
                            if (m_to_currency == comm)
                                throw std::invalid_argument (_("'Commodity From' can not be the same as 'Currency To'."));
                            m_from_commodity = comm;
                        }
                    }
                }
                break;

            case GncPricePropType::TO_CURRENCY:
                m_to_currency = boost::none;
                comm = parse_commodity_price_comm (value, GNC_COMMODITY_NS_CURRENCY); // Throws if parsing fails
                if (comm)
                {
                    if (m_from_commodity == comm)
                        throw std::invalid_argument (_("'Currency To' can not be the same as 'Commodity From'."));
                    if (gnc_commodity_is_currency (comm) != true)
                        throw std::invalid_argument (_("Value parsed into an invalid currency for a currency column type."));
                    m_to_currency = comm;
                }
                break;

            default:
                /* Issue a warning for all other prop_types. */
                PWARN ("%d is an invalid property for a Price", static_cast<int>(prop_type));
                break;
        }
    }
    catch (const std::invalid_argument& e)
    {
        auto err_str = (bl::format (std::string{_("Column '{1}' could not be understood.\n")}) %
                        std::string{_(gnc_price_col_type_strs[prop_type])}).str() +
                        e.what();
        m_errors.emplace(prop_type, err_str);
        throw std::invalid_argument (err_str);
    }
    catch (const std::out_of_range& e)
    {
        auto err_str = (bl::format (std::string{_("Column '{1}' could not be understood.\n")}) %
                        std::string{_(gnc_price_col_type_strs[prop_type])}).str() +
                        e.what();
        m_errors.emplace(prop_type, err_str);
        throw std::invalid_argument (err_str);
    }
}

void GncImportPrice::reset (GncPricePropType prop_type)
{
    try
    {
        if ((prop_type == GncPricePropType::FROM_NAMESPACE) ||
            (prop_type == GncPricePropType::FROM_SYMBOL))
             set_from_commodity (nullptr);

        if (prop_type == GncPricePropType::TO_CURRENCY)
             set_to_currency (nullptr);

        // set enable_test_empty to false to allow empty values
        set (prop_type, std::string(), false);
    }
    catch (...)
    {
        // Set with an empty string will effectively clear the property
        // but can also set an error for the property. Clear that error here.
        m_errors.erase(prop_type);
    }
}

std::string GncImportPrice::verify_essentials (void)
{
    /* Make sure this price has the minimum required set of properties defined */
    if (m_date == boost::none)
        return _("No date column.");
    else if (m_amount == boost::none)
        return _("No amount column.");
    else if (m_to_currency == boost::none)
        return _("No 'Currency to'.");
    else if (m_from_commodity == boost::none)
        return _("No 'Commodity from'.");
    else if (gnc_commodity_equal (*m_from_commodity, *m_to_currency))
        return _("'Commodity From' can not be the same as 'Currency To'.");
    else
        return std::string();
}

Result GncImportPrice::create_price (QofBook* book, GNCPriceDB *pdb, bool over)
{
    /* Gently refuse to create the price if the basics are not set correctly
     * This should have been tested before calling this function though!
     */
    auto check = verify_essentials();
    if (!check.empty())
    {
        PWARN ("Refusing to create price because essentials not set properly: %s", check.c_str());
        return FAILED;
    }

    auto date = static_cast<time64>(GncDateTime(*m_date, DayPart::neutral));

    auto amount = *m_amount;
    Result ret_val = ADDED;

    GNCPrice *old_price = gnc_pricedb_lookup_day_t64 (pdb, *m_from_commodity,
                                                      *m_to_currency, date);

    // Should old price be over written
    if ((old_price != nullptr) && (over == true))
    {
        DEBUG("Over write");
        gnc_pricedb_remove_price (pdb, old_price);
        gnc_price_unref (old_price);
        old_price = nullptr;
        ret_val = REPLACED;
    }

    char date_str [MAX_DATE_LENGTH + 1];
    memset (date_str, 0, sizeof(date_str));
    qof_print_date_buff (date_str, MAX_DATE_LENGTH, date);
    DEBUG("Date is %s, Commodity from is '%s', Currency is '%s', "
          "Amount is %s", date_str,
          gnc_commodity_get_fullname (*m_from_commodity),
          gnc_commodity_get_fullname (*m_to_currency),
          amount.to_string().c_str());
    // Create the new price
    if (old_price == nullptr)
    {
        DEBUG("Create");
        GNCPrice *price = gnc_price_create (book);
        gnc_price_begin_edit (price);

        gnc_price_set_commodity (price, *m_from_commodity);
        gnc_price_set_currency (price, *m_to_currency);

        int scu = gnc_commodity_get_fraction (*m_to_currency);
        auto amount_conv = amount.convert<RoundType::half_up>(scu * COMMODITY_DENOM_MULT);

        gnc_price_set_value (price, static_cast<gnc_numeric>(amount_conv));

        gnc_price_set_time64 (price, date);
        gnc_price_set_source (price, PRICE_SOURCE_USER_PRICE);
        gnc_price_set_typestr (price, PRICE_TYPE_LAST);
        gnc_price_commit_edit (price);

        bool perr = gnc_pricedb_add_price (pdb, price);

        gnc_price_unref (price);

        if (perr == false)
            throw std::invalid_argument (_("Failed to create price from selected columns."));
    }
    else
    {
        gnc_price_unref (old_price);
        ret_val = DUPLICATED;
    }
    return ret_val;
}

static std::string gen_err_str (std::map<GncPricePropType, std::string>& errors)
{
    auto full_error = std::string();
    for (auto error : errors)
    {
        full_error += (full_error.empty() ? "" : "\n") + error.second;
    }
    return full_error;
}

std::string GncImportPrice::errors ()
{
    return gen_err_str (m_errors);
}

