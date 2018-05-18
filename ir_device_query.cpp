#include "ir_device_query.h"
#include "ir_device_query_factory.h"
#include "memory_block.h"
#include "virtual_register.h"
#include "serial_device.h"

#include <cstring>

using namespace std;

namespace
{
    void PrintAddr(ostream & s, const PMemoryBlock & mb)
    {
        s << mb->Address;
    }

    // TPSet<PVirtualRegister> GetVirtualRegisters(const TPSet<PMemoryBlock> & memoryBlockSet)
    // {
    //     TPSet<PVirtualRegister> result;

    //     for (const auto & memoryBlock: memoryBlockSet) {
    //         const auto & localVirtualRegisters = memoryBlock->GetVirtualRegsiters();
    //         for (const auto & virtualRegister: localVirtualRegisters) {
    //             const auto & memoryBlocks = virtualRegister->GetMemoryBlocks();
    //             if (IsSubset(memoryBlockSet, memoryBlocks)) {
    //                 result.insert(virtualRegister);
    //             }
    //         }
    //     }

    //     return move(result);
    // }

    TPSet<PMemoryBlock> GetMemoryBlockSet(const vector<PVirtualRegister> & virtualRegisters)
    {
        TPSet<PMemoryBlock> memoryBlocks;

        for (const auto & reg: virtualRegisters) {
            auto memoryBlocks = reg->GetMemoryBlocks();
            memoryBlocks.insert(memoryBlocks.begin(), memoryBlocks.end());
        }

        return memoryBlocks;
    }

    TPSetRange<PMemoryBlock> GetMemoryBlockRange(const vector<PVirtualRegister> & virtualRegisters)
    {
        TPSet<PVirtualRegister> sortedRegs { virtualRegisters.begin(), virtualRegisters.end() };

        auto first = *(*sortedRegs.begin())->GetMemoryBlocks().begin();
        auto last = *(*sortedRegs.rbegin())->GetMemoryBlocks().rbegin();

        return TSerialDevice::StaticCreateMemoryBlockRange(first, last);
    }

    bool DetectHoles(const TPSetRange<PMemoryBlock> & memoryBlockRange)
    {
        int prev = -1;
        for (const auto & mb: memoryBlockRange) {
            if (prev >= 0 && (mb->Address - prev) > 1) {
                return true;
            }

            prev = mb->Address;
        }

        return false;
    }

    bool IsSameTypeAndSize(const TPSetRange<PMemoryBlock> & memoryBlockRange)
    {
        auto typeIndex = (*memoryBlockRange.begin())->Type.Index;
        auto size = (*memoryBlockRange.begin())->Size;

        return all_of(memoryBlockRange.begin(), memoryBlockRange.end(), [&](const PMemoryBlock & mb){
            return mb->Type.Index == typeIndex && mb->Size == size;
        });
    }
}

TIRDeviceQuery::TIRDeviceQuery(vector<PVirtualRegister> && virtualRegisters, EQueryOperation operation)
    : MemoryBlockRange(GetMemoryBlockRange(virtualRegisters))
    , VirtualRegisters(move(virtualRegisters))
    , HasHoles(DetectHoles(MemoryBlockRange))
    , Operation(operation)
    , Status(EQueryStatus::NotExecuted)
{
    AbleToSplit = (VirtualRegisters.size() > 1);

    assert(IsSameTypeAndSize(MemoryBlockRange));
}

bool TIRDeviceQuery::operator<(const TIRDeviceQuery & rhs) const noexcept
{
    return *MemoryBlockRange.GetLast() < *rhs.MemoryBlockRange.GetFirst();
}

PSerialDevice TIRDeviceQuery::GetDevice() const
{
    return MemoryBlockRange.GetFirst()->GetDevice();
}

uint32_t TIRDeviceQuery::GetBlockCount() const
{
    return (MemoryBlockRange.GetLast()->Address - MemoryBlockRange.GetFirst()->Address) + 1;
}

uint32_t TIRDeviceQuery::GetValueCount() const
{
    return GetBlockCount() * GetType().GetValueCount();
}

uint32_t TIRDeviceQuery::GetStart() const
{
    return MemoryBlockRange.GetFirst()->Address;
}

uint16_t TIRDeviceQuery::GetBlockSize() const
{
    return (*MemoryBlockRange.begin())->Size;    // it is guaranteed that all blocks in query have same size and type
}

uint32_t TIRDeviceQuery::GetSize() const
{
    return GetBlockSize() * GetBlockCount();
}

const TMemoryBlockType & TIRDeviceQuery::GetType() const
{
    return MemoryBlockRange.GetFirst()->Type;
}

const string & TIRDeviceQuery::GetTypeName() const
{
    return MemoryBlockRange.GetFirst()->GetTypeName();
}

void TIRDeviceQuery::SetStatus(EQueryStatus status) const
{
    Status = status;

    if (Status != EQueryStatus::NotExecuted && Status != EQueryStatus::Ok) {
        if (Operation == EQueryOperation::Read) {
            for (const auto & virtualRegister: VirtualRegisters) {
                virtualRegister->UpdateReadError(true);
            }
        } else if (Operation == EQueryOperation::Write) {
            for (const auto & virtualRegister: VirtualRegisters) {
                virtualRegister->UpdateWriteError(true);
            }
        }
    }
}

void TIRDeviceQuery::SetStatus(EQueryStatus status)
{
    static_cast<const TIRDeviceQuery *>(this)->SetStatus(status);
}

EQueryStatus TIRDeviceQuery::GetStatus() const
{
    return Status;
}

void TIRDeviceQuery::ResetStatus()
{
    SetStatus(EQueryStatus::NotExecuted);
}

void TIRDeviceQuery::InvalidateReadValues()
{
    assert(Operation == EQueryOperation::Read);

    for (const auto & reg: VirtualRegisters) {
        reg->InvalidateReadValues();
    }
}

void TIRDeviceQuery::SetEnabledWithRegisters(bool enabled)
{
    for (const auto & reg: VirtualRegisters) {
        reg->SetEnabled(enabled);
    }
}

bool TIRDeviceQuery::IsEnabled() const
{
    return any_of(VirtualRegisters.begin(), VirtualRegisters.end(), [](const PVirtualRegister & reg){
        return reg->IsEnabled();
    });
}

bool TIRDeviceQuery::IsExecuted() const
{
    return Status != EQueryStatus::NotExecuted;
}

bool TIRDeviceQuery::IsAbleToSplit() const
{
    return AbleToSplit;
}

void TIRDeviceQuery::SetAbleToSplit(bool ableToSplit)
{
    AbleToSplit = ableToSplit;
}

string TIRDeviceQuery::Describe() const
{
    return PrintRange(MemoryBlockRange.begin(), MemoryBlockRange.end(), PrintAddr);
}

string TIRDeviceQuery::DescribeOperation() const
{
    switch(Operation) {
        case EQueryOperation::Read:
            return "read";
        case EQueryOperation::Write:
            return "write";
        default:
            return "<unknown operation (code: " + to_string((int)Operation) + ")>";
    }
}

TIRDeviceMemoryView TIRDeviceQuery::CreateMemoryView(void * mem, size_t size) const
{
    assert(GetSize() == size);

    return { static_cast<uint8_t *>(mem), size, GetType(), GetStart(), GetBlockSize() };
}

TIRDeviceMemoryView TIRDeviceQuery::CreateMemoryView(const void * mem, size_t size) const
{
    assert(GetSize() == size);

    return { static_cast<const uint8_t *>(mem), size, GetType(), GetStart(), GetBlockSize() };
}

void TIRDeviceQuery::FinalizeRead(const TIRDeviceMemoryView & memoryView) const
{
    assert(Operation == EQueryOperation::Read);
    assert(GetStatus() == EQueryStatus::NotExecuted);
    assert(GetSize() == memoryView.Size);

    for (const auto & mb: MemoryBlockRange) {
        mb->CacheIfNeeded(memoryView[mb]);
    }

    for (const auto & reg: VirtualRegisters) {
        reg->AcceptDeviceValue(memoryView.ReadValue(reg->GetValueDesc()));
    }

    SetStatus(EQueryStatus::Ok);
}

TIRDeviceValueQuery::TIRDeviceValueQuery(vector<PVirtualRegister> && virtualRegisters, EQueryOperation operation)
    : TIRDeviceQuery(move(virtualRegisters), operation)
    , MemoryBlocks(GetMemoryBlockSet(VirtualRegisters))
{
    assert(!MemoryBlocks.empty());
}

void TIRDeviceValueQuery::SetValue(const TIRDeviceValueDesc & valueDesc, uint64_t value) const
{
    //MemoryView.WriteValue(valueDesc, value);
}

void TIRDeviceValueQuery::FinalizeWrite(const TIRDeviceMemoryView & memoryView) const
{
    assert(Operation == EQueryOperation::Write);
    assert(GetStatus() == EQueryStatus::NotExecuted);

    for (const auto & mb: MemoryBlockRange) {
        mb->CacheIfNeeded(memoryView[mb]);
    }

    for (const auto & reg: VirtualRegisters) {
        reg->AcceptWriteValue();
    }

    SetStatus(EQueryStatus::Ok);
}

TIRDeviceMemoryView TIRDeviceValueQuery::GetValuesImpl(void * mem, size_t size) const
{
    assert(GetSize() == size);

    // auto itMemoryBlock = MemoryBlockRange.First;
    // auto itMemoryBlockValue = MemoryBlockValues.begin();
    // auto bytes = static_cast<uint8_t*>(mem);

    const auto & memoryView = CreateMemoryView(mem, size);
    memoryView.Clear();

    for (const auto & mb: MemoryBlockRange) {
        memoryView[mb] = mb->GetCache();
    }

    for (const auto & reg: VirtualRegisters) {
        memoryView.WriteValue(reg->GetValueDesc(), reg->ValueToWrite);
    }

    return memoryView;

    // assert(*itMemoryBlock == itMemoryBlockValue->first);

    // for (uint32_t i = 0; i < count; ++i) {
    //     const auto requestedRegisterAddress = GetStart() + i;

    //     assert(itMemoryBlock != MemoryBlockRange.end());
    //     assert(itMemoryBlockValue != MemoryBlockValues.end());

    //     // try read value from query itself
    //     {
    //         const auto & memoryBlock = itMemoryBlockValue->first;
    //         const auto & value = itMemoryBlockValue->second;

    //         if (memoryBlock->Address == requestedRegisterAddress) {    // this register exists and query has its value - write from query
    //             memcpy(bytes, value, size);

    //             if (Global::Debug) {
    //                 std::cerr << "TIRDeviceValueQueryImpl::GetValuesImpl: read address '" << requestedRegisterAddress << "' from query: '" << /*value*/ "TODO: output memory" << "'" << std::endl;
    //             }

    //             ++itMemoryBlock;
    //             ++itMemoryBlockValue;
    //             bytes += size;
    //             continue;
    //         }
    //     }

    //     // try read value from cache
    //     {
    //         const auto & memoryBlock = *itMemoryBlock;

    //         if (memoryBlock->Address == requestedRegisterAddress) {    // this register exists but query doesn't have value for it - write cached value
    //             const auto & cache = memoryBlock->GetCache();
    //             assert(cache);
    //             memcpy(bytes, cache, size);

    //             if (Global::Debug) {
    //                 std::cerr << "TIRDeviceValueQueryImpl::GetValuesImpl: read address '" << requestedRegisterAddress << "' from cache: '" << /*memoryBlock->GetValue()*/ "TODO: output memory" << "'" << std::endl;
    //             }
    //             ++itMemoryBlock;
    //             bytes += size;
    //             continue;
    //         }
    //     }

    //     // driver doesn't use this address (hole) - fill with zeroes
    //     {
    //         if (Global::Debug) {
    //             std::cerr << "TIRDeviceValueQueryImpl::GetValuesImpl: zfill address '" << requestedRegisterAddress << "'" << std::endl;
    //         }
    //         memset(bytes, 0, size);
    //         bytes += size;
    //     }
    // }

    // assert(itMemoryBlock == MemoryBlockRange.end());
    // assert(itMemoryBlockValue == MemoryBlockValues.end());
}

string TIRDeviceQuerySet::Describe() const
{
    ostringstream ss;

    return PrintCollection(Queries, [](ostream & s, const PIRDeviceQuery & query){
        s << "\t" << query->Describe();
    }, true, "");
}

TIRDeviceQuerySet::TIRDeviceQuerySet(const vector<PVirtualRegister> & virtualRegisters, EQueryOperation operation)
{
    Queries = TIRDeviceQueryFactory::GenerateQueries(virtualRegisters, operation);

    if (Global::Debug) {
        cerr << "Initialized query set: " << Describe() << endl;
    }

    assert(!Queries.empty());
}

PSerialDevice TIRDeviceQuerySet::GetDevice() const
{
    assert(!Queries.empty());

    const auto & query = *Queries.begin();

    return query->GetDevice();
}
