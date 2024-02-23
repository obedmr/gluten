/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <Common/LocalDate.h>
#include <Common/DateLUT.h>
#include <Common/DateLUTImpl.h>
#include <Columns/ColumnsDateTime.h>
#include <DataTypes/DataTypeDateTime64.h>
#include <Functions/FunctionsConversion.h>
#include <Functions/FunctionFactory.h>
#include <IO/ReadBufferFromMemory.h>
#include <IO/parseDateTimeBestEffort.h>
#include <IO/ReadHelpers.h>
#include <ctime>

using namespace DB;

namespace DB
{
namespace ErrorCodes
{
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
    extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
    extern const int ILLEGAL_COLUMN;
}
}

namespace local_engine
{
class SparkFunctionConvertToDateTime : public DB::FunctionToDateTime64OrNull
{
public:
    static constexpr auto name = "sparkToDateTime";
    static DB::FunctionPtr create(DB::ContextPtr) { return std::make_shared<SparkFunctionConvertToDateTime>(); }
    SparkFunctionConvertToDateTime() = default;
    ~SparkFunctionConvertToDateTime() override = default;
    String getName() const override { return name; }

    bool checkDateTimeFormat(DB::ReadBuffer & buf, size_t buf_size, UInt8 & can_be_parsed) const
    {        
        auto checkNumbericASCII = [&](DB::ReadBuffer & rb, size_t start, size_t length) -> bool
        {
            for (size_t i = start; i < start + length; ++i)
            {
                if (!isNumericASCII(*(rb.position() + i)))
                    return false;
            }
            return true;
        };
        auto checkDelimiter = [&](DB::ReadBuffer & rb, size_t pos, char delim) -> bool
        {
            if (*(rb.position() + pos) != delim)
                return false;
            else
                return true;
        };
        if ((buf_size == 10 || buf_size == 11)
            && checkNumbericASCII(buf, 0, 4) && checkDelimiter(buf, 4, '-')
            && checkNumbericASCII(buf, 5, 2) && checkDelimiter(buf, 7, '-')
            && checkNumbericASCII(buf, 8, 2))
        {
            if (buf_size == 10)
                return true;
            else if (*(buf.position() + 10) != ' ')
                can_be_parsed = 0;
            return false;
        }
        else if ((buf_size == 19 || buf_size == 20) 
            && (checkNumbericASCII(buf, 0, 4) && checkDelimiter(buf, 4, '-')
            && checkNumbericASCII(buf, 5, 2) && checkDelimiter(buf, 7, '-')
            && checkNumbericASCII(buf, 8, 2) && checkDelimiter(buf, 10, ' ')
            && checkNumbericASCII(buf, 11, 2) && checkDelimiter(buf, 13, ':')
            && checkNumbericASCII(buf, 14, 2) && checkDelimiter(buf, 16, ':')
            && checkNumbericASCII(buf, 17, 2)))
        {
            if (buf_size == 19)
                return true;
            else
                return *(buf.position() + 19) == '.';
        }
        else if (buf_size < 4 || !isNumericASCII(*(buf.position() + buf_size - 1)))
        {
            can_be_parsed = 0;
            return false;
        }
        else if (buf_size < 19)
            return false;
        else if (buf_size > 20)
        {
            for (size_t i = 20; i < buf_size; ++i)
            {
                if (!isNumericASCII(*(buf.position() + i)))
                    return false;
            }
        }
        return true;
    }

    DB::DataTypePtr getReturnTypeImpl(const ColumnsWithTypeAndName & arguments) const override
    {
        UInt32 scale = 6;
        if (arguments.size() > 1)
            scale = extractToDecimalScale(arguments[1]);
        const auto timezone = extractTimeZoneNameFromFunctionArguments(arguments, 2, 0, false);
        return makeNullable(std::make_shared<DataTypeDateTime64>(scale, timezone));
    }

    DB::ColumnPtr executeImpl(const DB::ColumnsWithTypeAndName & arguments, const DB::DataTypePtr & result_type, size_t input_rows) const override
    {
         if (arguments.size() != 1 && arguments.size() != 2)
            throw DB::Exception(DB::ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH, "Function {}'s arguments number must be 1 or 2.", name);
        
        if (!result_type->isNullable())
            throw DB::Exception(DB::ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT, "Function {}'s return type must be nullable", name);
        
        if (!isDateTime64(removeNullable(result_type)))
            throw DB::Exception(DB::ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT, "Function {}'s return type must be datetime.", name);
        
        size_t size = arguments[0].column->size();
        const DB::DataTypeDateTime64 * datetime_64_type = checkAndGetDataType<DB::DataTypeDateTime64>(removeNullable(result_type).get());
        UInt32 scale = datetime_64_type->getScale();
        auto data_col = DB::ColumnDateTime64::create(size, scale);
        auto null_map_col = DB::ColumnUInt8::create(size);
        executeInternal(arguments[0].column, scale, data_col->getData(), null_map_col->getData());
        return DB::ColumnNullable::create(std::move(data_col), std::move(null_map_col));
    }

    void executeInternal(const DB::ColumnPtr & src, const UInt32 & scale, PaddedPODArray<DB::DateTime64> & dst_data, PaddedPODArray<UInt8> & null_map_data) const
    {
        const DateLUTImpl * local_time_zone = &DateLUT::instance();
        const DateLUTImpl * utc_time_zone = &DateLUT::instance("UTC");
        for (size_t i = 0; i < src->size(); ++i)
        {
            const StringRef data = src->getDataAt(i);
            DB::ReadBufferFromMemory buf(data.data, data.size);
            while(!buf.eof() && *buf.position() == ' ')
            {
                    buf.position() ++;
            }
            UInt8 can_be_parsed = 1;
            if (checkDateTimeFormat(buf, buf.buffer().end() - buf.position(), can_be_parsed) && can_be_parsed)
            {
                readDateTime64Text(dst_data[i], scale, buf, *local_time_zone);
                null_map_data[i] = 0;
            }
            else if (!can_be_parsed)
            {
                dst_data[i] = 0;
                null_map_data[i] = 1;
            }
            else
            {
                parseDateTime64BestEffort(dst_data[i], scale, buf, *local_time_zone, *utc_time_zone);
                null_map_data[i] = 0;
            }
        }
    }
};

}
