// Aleth: Ethereum C++ client, tools and libraries.
// Copyright 2014-2019 Aleth Authors.
// Licensed under the GNU General Public License, Version 3.

#pragma once

#include "Account.h"
#include "BlockDetails.h"
#include "BlockQueue.h"
#include "ChainParams.h"
#include "DatabasePaths.h"
#include "LastBlockHashesFace.h"
#include "State.h"
#include "Transaction.h"
#include "VerifiedBlock.h"
#include <libdevcore/Exceptions.h>
#include <libdevcore/Guards.h>
#include <libdevcore/Log.h>
#include <libdevcore/db.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/Common.h>
#include <libethcore/SealEngine.h>
#include <boost/filesystem/path.hpp>
#include <chrono>
#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace std
{
template <> struct hash<pair<dev::h256, unsigned>>
{
    size_t operator()(pair<dev::h256, unsigned> const& _x) const { return hash<dev::h256>()(_x.first) ^ hash<unsigned>()(_x.second); }
};
}

namespace dev
{

class OverlayDB;

namespace eth
{

static const h256s NullH256s;

class State;
class Block;
class ImportPerformanceLogger;

DEV_SIMPLE_EXCEPTION(AlreadyHaveBlock);
DEV_SIMPLE_EXCEPTION(FutureTime);
DEV_SIMPLE_EXCEPTION(TransientError);
DEV_SIMPLE_EXCEPTION(FailedToWriteChainStart);
DEV_SIMPLE_EXCEPTION(UnknownBlockNumber);

// TODO: Move all this Genesis stuff into Genesis.h/.cpp
std::unordered_map<Address, Account> const& genesisState();

db::Slice toSlice(h256 const& _h, unsigned _sub = 0);
db::Slice toSlice(uint64_t _n, unsigned _sub = 0);

using BlocksHash = std::unordered_map<h256, bytes>;
using TransactionHashes = h256s;
using UncleHashes = h256s;

enum {
    ExtraDetails = 0,
    ExtraBlockHash,
    ExtraTransactionAddress,
    ExtraLogBlooms,
    ExtraReceipts,
    ExtraBlocksBlooms
};

using ProgressCallback = std::function<void(unsigned, unsigned)>;

class VersionChecker
{
public:
    VersionChecker(boost::filesystem::path const& _dbPath, h256 const& _genesisHash);
};

/**
 * @brief Implements the blockchain database. All data this gives is disk-backed.
 * @threadsafe
 */
class BlockChain
{
public:
    /// Doesn't open the database - if you want it open it's up to you to subclass this and open it
    /// in the constructor there.
    BlockChain(ChainParams const& _p, boost::filesystem::path const& _path, WithExisting _we = WithExisting::Trust, ProgressCallback const& _pc = ProgressCallback());
    ~BlockChain();

    /// Reopen everything.
    void reopen(WithExisting _we = WithExisting::Trust, ProgressCallback const& _pc = ProgressCallback()) { reopen(m_params, _we, _pc); }
    void reopen(ChainParams const& _p, WithExisting _we = WithExisting::Trust, ProgressCallback const& _pc = ProgressCallback());

    /// (Potentially) renders invalid existing bytesConstRef returned by lastBlock.
    /// To be called from main loop every 100ms or so.
    void process();

    /// Sync the chain with any incoming blocks. All blocks should, if processed in order.
    /// @returns tuple with three members - the first (ImportRoute) contains hashes of fresh blocks
    /// and dead blocks as well as a list of imported transactions. The second is a bool which is
    /// true iff there are additional blocks to be processed. The third is the imported block count.
    std::tuple<ImportRoute, bool, unsigned> sync(
        BlockQueue& _bq, OverlayDB const& _stateDB, unsigned _max);

    /// Import the supplied blocks into the chain. Blocks should be processed in order.
    /// @returns a tuple with three members - the first (ImportRoute) contains fresh blocks, dead
    /// blocks and imported transactions. The second contains hashes of bad blocks. The third
    /// contains the imported block count.
    std::tuple<ImportRoute, h256s, unsigned> sync(
        VerifiedBlocks const& _blocks, OverlayDB const& _stateDB);

    /// Attempt to import the given block directly into the BlockChain and sync with the state DB.
    /// @returns the block hashes of any blocks that came into/went out of the canonical block chain.
    std::pair<ImportResult, ImportRoute> attemptImport(bytes const& _block, OverlayDB const& _stateDB, bool _mutBeNew = true) noexcept;

    /// Import block into disk-backed DB.
    /// @returns the block hashes of any blocks that came into/went out of the canonical block chain.
    ImportRoute import(bytes const& _block, OverlayDB const& _stateDB, bool _mustBeNew = true);
    ImportRoute import(VerifiedBlockRef const& _block, OverlayDB const& _db, bool _mustBeNew = true);

    /// Import data into disk-backed DB.
    /// This will not execute the block and populate the state trie, but rather will simply add the
    /// block/header and receipts directly into the databases.
    void insert(bytes const& _block, bytesConstRef _receipts, bool _mustBeNew = true);
    void insert(VerifiedBlockRef _block, bytesConstRef _receipts, bool _mustBeNew = true);
    /// Insert that doesn't require parent to be imported, useful when we don't have the full blockchain (like restoring from partial snapshot).
    ImportRoute insertWithoutParent(bytes const& _block, bytesConstRef _receipts, u256 const& _totalDifficulty);

    /// Returns true if the given block is known (though not necessarily a part of the canon chain).
    bool isKnown(h256 const& _hash, bool _isCurrent = true) const;

    /// Get the partial-header of a block (or the most recent mined if none given). Thread-safe.
    BlockHeader info(h256 const& _hash) const { return BlockHeader(headerData(_hash), HeaderData); }
    BlockHeader info() const { return info(currentHash()); }

    /// Get a block (RLP format) for the given hash (or the most recent mined if none given). Thread-safe.
    bytes block(h256 const& _hash) const;
    bytes block() const { return block(currentHash()); }

    /// Get a block (RLP format) for the given hash (or the most recent mined if none given). Thread-safe.
    bytes headerData(h256 const& _hash) const;
    bytes headerData() const { return headerData(currentHash()); }

    /// Get the familial details concerning a block (or the most recent mined if none given). Thread-safe.
    BlockDetails details(h256 const& _hash) const { return queryExtras<BlockDetails, ExtraDetails>(_hash, m_details, x_details, NullBlockDetails); }
    BlockDetails details() const { return details(currentHash()); }

    /// Get the transactions' log blooms of a block (or the most recent mined if none given). Thread-safe.
    BlockLogBlooms logBlooms(h256 const& _hash) const { return queryExtras<BlockLogBlooms, ExtraLogBlooms>(_hash, m_logBlooms, x_logBlooms, NullBlockLogBlooms); }
    BlockLogBlooms logBlooms() const { return logBlooms(currentHash()); }

    /// Get the transactions' receipts of a block (or the most recent mined if none given). Thread-safe.
    /// receipts are given in the same order are in the same order as the transactions
    BlockReceipts receipts(h256 const& _hash) const { return queryExtras<BlockReceipts, ExtraReceipts>(_hash, m_receipts, x_receipts, NullBlockReceipts); }
    BlockReceipts receipts() const { return receipts(currentHash()); }

    /// Get the transaction by block hash and index;
    TransactionReceipt transactionReceipt(h256 const& _blockHash, unsigned _i) const { return receipts(_blockHash).receipts[_i]; }

    /// Get the transaction receipt by transaction hash. Thread-safe.
    TransactionReceipt transactionReceipt(h256 const& _transactionHash) const { TransactionAddress ta = queryExtras<TransactionAddress, ExtraTransactionAddress>(_transactionHash, m_transactionAddresses, x_transactionAddresses, NullTransactionAddress); if (!ta) return bytesConstRef(); return transactionReceipt(ta.blockHash, ta.index); }

    /// Get a list of transaction hashes for a given block. Thread-safe.
    TransactionHashes transactionHashes(h256 const& _hash) const { auto b = block(_hash); RLP rlp(b); h256s ret; for (auto t: rlp[1]) ret.push_back(sha3(t.data())); return ret; }
    TransactionHashes transactionHashes() const { return transactionHashes(currentHash()); }

    /// Get a list of uncle hashes for a given block. Thread-safe.
    UncleHashes uncleHashes(h256 const& _hash) const { auto b = block(_hash); RLP rlp(b); h256s ret; for (auto t: rlp[2]) ret.push_back(sha3(t.data())); return ret; }
    UncleHashes uncleHashes() const { return uncleHashes(currentHash()); }
    
    /// Get the hash for a given block's number.
    h256 numberHash(unsigned _i) const { if (!_i) return genesisHash(); return queryExtras<BlockHash, uint64_t, ExtraBlockHash>(_i, m_blockHashes, x_blockHashes, NullBlockHash).value; }

    LastBlockHashesFace const& lastBlockHashes() const { return *m_lastBlockHashes;  }

    int chainID() const { return m_params.chainID; }

    /** Get the block blooms for a number of blocks. Thread-safe.
     * @returns the object pertaining to the blocks:
     * level 0:
     * 0x, 0x + 1, .. (1x - 1)
     * 1x, 1x + 1, .. (2x - 1)
     * ...
     * (255x .. (256x - 1))
     * level 1:
     * 0x .. (1x - 1), 1x .. (2x - 1), ..., (255x .. (256x - 1))
     * 256x .. (257x - 1), 257x .. (258x - 1), ..., (511x .. (512x - 1))
     * ...
     * level n, index i, offset o:
     * i * (x ^ n) + o * x ^ (n - 1)
     */
    BlocksBlooms blocksBlooms(unsigned _level, unsigned _index) const { return blocksBlooms(chunkId(_level, _index)); }
    BlocksBlooms blocksBlooms(h256 const& _chunkId) const { return queryExtras<BlocksBlooms, ExtraBlocksBlooms>(_chunkId, m_blocksBlooms, x_blocksBlooms, NullBlocksBlooms); }
    LogBloom blockBloom(unsigned _number) const { return blocksBlooms(chunkId(0, _number / c_bloomIndexSize)).blooms[_number % c_bloomIndexSize]; }
    std::vector<unsigned> withBlockBloom(LogBloom const& _b, unsigned _earliest, unsigned _latest) const;
    std::vector<unsigned> withBlockBloom(LogBloom const& _b, unsigned _earliest, unsigned _latest, unsigned _topLevel, unsigned _index) const;

    /// Returns true if transaction is known. Thread-safe
    bool isKnownTransaction(h256 const& _transactionHash) const { TransactionAddress ta = queryExtras<TransactionAddress, ExtraTransactionAddress>(_transactionHash, m_transactionAddresses, x_transactionAddresses, NullTransactionAddress); return !!ta; }

    /// Get a transaction from its hash. Thread-safe.
    bytes transaction(h256 const& _transactionHash) const { TransactionAddress ta = queryExtras<TransactionAddress, ExtraTransactionAddress>(_transactionHash, m_transactionAddresses, x_transactionAddresses, NullTransactionAddress); if (!ta) return bytes(); return transaction(ta.blockHash, ta.index); }
    std::pair<h256, unsigned> transactionLocation(h256 const& _transactionHash) const { TransactionAddress ta = queryExtras<TransactionAddress, ExtraTransactionAddress>(_transactionHash, m_transactionAddresses, x_transactionAddresses, NullTransactionAddress); if (!ta) return std::pair<h256, unsigned>(h256(), 0); return std::make_pair(ta.blockHash, ta.index); }

    /// Get a block's transaction (RLP format) for the given block hash (or the most recent mined if none given) & index. Thread-safe.
    bytes transaction(h256 const& _blockHash, unsigned _i) const { bytes b = block(_blockHash); return RLP(b)[1][_i].data().toBytes(); }
    bytes transaction(unsigned _i) const { return transaction(currentHash(), _i); }

    /// Get all transactions from a block.
    std::vector<bytes> transactions(h256 const& _blockHash) const { bytes b = block(_blockHash); std::vector<bytes> ret; for (auto const& i: RLP(b)[1]) ret.push_back(i.data().toBytes()); return ret; }
    std::vector<bytes> transactions() const { return transactions(currentHash()); }

    /// Get a number for the given hash (or the most recent mined if none given). Thread-safe.
    unsigned number(h256 const& _hash) const { return details(_hash).number; }
    unsigned number() const { ReadGuard l(x_lastBlockHash); return m_lastBlockNumber; }


    h256 currentHash() const { ReadGuard l(x_lastBlockHash); return m_lastBlockHash; } /// Get a given block (RLP format). Thread-safe.


    h256 genesisHash() const { return m_genesisHash; } /// Get the hash of the genesis block. Thread-safe.

    /// Get all blocks not allowed as uncles given a parent (i.e. featured as uncles/main in parent, parent + 1, ... parent + @a _generations).
    /// @returns set including the header-hash of every parent (including @a _parent) up to and including generation + @a _generations
    /// togther with all their quoted uncles.
    h256Hash allKinFrom(h256 const& _parent, unsigned _generations) const;

    /// Run through database and verify all blocks by reevaluating.
    /// Will call _progress with the progress in this operation first param done, second total.
    void rebuild(boost::filesystem::path const& _path,
        ProgressCallback const& _progress = std::function<void(unsigned, unsigned)>());

    /// Alter the head of the chain to some prior block along it.
    void rewind(unsigned _newHead);

    /// Rescue the database.
    void rescue(OverlayDB const& _db);

    /** @returns a tuple of:
     * - an vector of hashes of all blocks between @a _from and @a _to, all blocks are ordered first by a number of
     * blocks that are parent-to-child, then two sibling blocks, then a number of blocks that are child-to-parent;
     * - the block hash of the latest common ancestor of both blocks;
     * - the index where the latest common ancestor of both blocks would either be found or inserted, depending
     * on whether it is included.
     *
     * @param _common if true, include the common ancestor in the returned vector.
     * @param _pre if true, include all block hashes running from @a _from until the common ancestor in the returned vector.
     * @param _post if true, include all block hashes running from the common ancestor until @a _to in the returned vector.
     *
     * e.g. if the block tree is 3a -> 2a -> 1a -> g and 2b -> 1b -> g (g is genesis, *a, *b are competing chains),
     * then:
     * @code
     * treeRoute(3a, 2b, false) == make_tuple({ 3a, 2a, 1a, 1b, 2b }, g, 3);
     * treeRoute(2a, 1a, false) == make_tuple({ 2a, 1a }, 1a, 1)
     * treeRoute(1a, 2a, false) == make_tuple({ 1a, 2a }, 1a, 0)
     * treeRoute(1b, 2a, false) == make_tuple({ 1b, 1a, 2a }, g, 1)
     * treeRoute(3a, 2b, true) == make_tuple({ 3a, 2a, 1a, g, 1b, 2b }, g, 3);
     * treeRoute(2a, 1a, true) == make_tuple({ 2a, 1a }, 1a, 1)
     * treeRoute(1a, 2a, true) == make_tuple({ 1a, 2a }, 1a, 0)
     * treeRoute(1b, 2a, true) == make_tuple({ 1b, g, 1a, 2a }, g, 1)
     * @endcode
     */
    std::tuple<h256s, h256, unsigned> treeRoute(h256 const& _from, h256 const& _to, bool _common = true, bool _pre = true, bool _post = true) const;

    struct Statistics
    {
        unsigned memBlocks = 0;
        unsigned memDetails = 0;
        unsigned memLogBlooms = 0;
        unsigned memReceipts = 0;
        unsigned memTransactionAddresses = 0;
        unsigned memBlockHashes = 0;
        unsigned memTotal() const { return memBlocks + memDetails + memLogBlooms + memReceipts + memTransactionAddresses + memBlockHashes; }
    };

    /// @returns statistics about memory usage.
    Statistics usage(bool _freshen = false) const { if (_freshen) updateStats(); return m_lastStats; }

    /// Deallocate unused data.
    void garbageCollect(bool _force = false);

    /// Change the function that is called with a bad block.
    void setOnBad(std::function<void(Exception&)> _t) { m_onBad = _t; }

    /// Change the function that is called when a new block is imported
    void setOnBlockImport(std::function<void(BlockHeader const&)> _t) { m_onBlockImport = _t; }

    /// Get a pre-made genesis State object.
    Block genesisBlock(OverlayDB const& _db) const;

    /// Verify block and prepare it for enactment
    VerifiedBlockRef verifyBlock(bytesConstRef _block, std::function<void(Exception&)> const& _onBad, ImportRequirements::value _ir = ImportRequirements::OutOfOrderChecks) const;

    /// Gives a dump of the blockchain database. For debug/test use only.
    std::string dumpDatabase() const;

    ChainParams const& chainParams() const { return m_params; }

    SealEngineFace* sealEngine() const { return m_sealEngine.get(); }

    BlockHeader const& genesis() const;

    /// @returns first block number of the chain, non-zero when we have partial chain e.g. after snapshot import.
    unsigned chainStartBlockNumber() const;
    /// Change the chain start block.
    void setChainStartBlockNumber(unsigned _number);

private:
    static h256 chunkId(unsigned _level, unsigned _index) { return h256(_index * 0xff + _level); }

    /// Initialise everything and ready for openning the database.
    void init(ChainParams const& _p);
    /// Open the database. Returns whether or not the database needs to be rebuilt.
    bool open(boost::filesystem::path const& _path, WithExisting _we);
    /// Open the database, rebuilding if necessary.
    void open(boost::filesystem::path const& _path, WithExisting _we, ProgressCallback const& _pc);
    /// Finalise everything and close the database.
    void close();

    ImportRoute insertBlockAndExtras(VerifiedBlockRef const& _block, bytesConstRef _receipts, u256 const& _totalDifficulty, ImportPerformanceLogger& _performanceLogger);
    void checkBlockIsNew(VerifiedBlockRef const& _block) const;
    void checkBlockTimestamp(BlockHeader const& _header) const;

    template <class T, class K, unsigned N>
    T queryExtras(K const& _h, std::unordered_map<K, T>& _m, boost::shared_mutex& _x, T const& _n,
        db::DatabaseFace* _extrasDB = nullptr) const
    {
        {
            ReadGuard l(_x);
            auto it = _m.find(_h);
            if (it != _m.end())
                return it->second;
        }

        std::string const s = (_extrasDB ? _extrasDB : m_extrasDB.get())->lookup(toSlice(_h, N));
        if (s.empty())
            return _n;

        noteUsed(_h, N);

        WriteGuard l(_x);
        auto ret = _m.insert(std::make_pair(_h, T(RLP(s))));
        return ret.first->second;
    }

    template <class T, unsigned N>
    T queryExtras(h256 const& _h, std::unordered_map<h256, T>& _m, boost::shared_mutex& _x,
        T const& _n, db::DatabaseFace* _extrasDB = nullptr) const
    {
        return queryExtras<T, h256, N>(_h, _m, _x, _n, _extrasDB);
    }

    void checkConsistency();

    /// Clears all caches from the tip of the chain up to (including) _firstInvalid.
    /// These include the blooms, the block hashes and the transaction lookup tables.
    void clearCachesDuringChainReversion(unsigned _firstInvalid);
    void clearBlockBlooms(unsigned _begin, unsigned _end);

    /// The caches of the disk DB and their locks.
    mutable SharedMutex x_blocks;
    mutable BlocksHash m_blocks;
    mutable SharedMutex x_details;
    mutable BlockDetailsHash m_details;
    mutable SharedMutex x_logBlooms;
    mutable BlockLogBloomsHash m_logBlooms;
    mutable SharedMutex x_receipts;
    mutable BlockReceiptsHash m_receipts;
    mutable SharedMutex x_transactionAddresses;
    mutable TransactionAddressHash m_transactionAddresses;
    mutable SharedMutex x_blockHashes;
    mutable BlockHashHash m_blockHashes;
    mutable SharedMutex x_blocksBlooms;
    mutable BlocksBloomsHash m_blocksBlooms;

    using CacheID = std::pair<h256, unsigned>;
    mutable Mutex x_cacheUsage;
    mutable std::deque<std::unordered_set<CacheID>> m_cacheUsage;
    mutable std::unordered_set<CacheID> m_inUse;
    void noteUsed(h256 const& _h, unsigned _extra = (unsigned)-1) const;
    void noteUsed(uint64_t const& _h, unsigned _extra = (unsigned)-1) const { (void)_h; (void)_extra; } // don't note non-hash types
    std::chrono::system_clock::time_point m_lastCollection;

    void noteCanonChanged() const { m_lastBlockHashes->clear(); }
    std::unique_ptr<LastBlockHashesFace> m_lastBlockHashes;

    void updateStats() const;
    mutable Statistics m_lastStats;

    /// The disk DBs. Thread-safe, so no need for locks.
    std::unique_ptr<db::DatabaseFace> m_blocksDB;
    std::unique_ptr<db::DatabaseFace> m_extrasDB;

    /// Hash of the last (valid) block on the longest chain.
    mutable boost::shared_mutex x_lastBlockHash; // should protect both m_lastBlockHash and m_lastBlockNumber
    h256 m_lastBlockHash;
    unsigned m_lastBlockNumber = 0;

    ChainParams m_params;
    std::shared_ptr<SealEngineFace> m_sealEngine;   // consider shared_ptr.
    mutable SharedMutex x_genesis;
    mutable BlockHeader m_genesis;  // mutable because they're effectively memos.
    mutable bytes m_genesisHeaderBytes; // mutable because they're effectively memos.
    mutable h256 m_genesisHash;     // mutable because they're effectively memos.

    std::unique_ptr<DatabasePaths> m_dbPaths; // Paths for various databases (e.g. blocks, extras)

    std::function<void(Exception&)> m_onBad;                                    ///< Called if we have a block that doesn't verify.
    std::function<void(BlockHeader const&)> m_onBlockImport;                                        ///< Called if we have imported a new block into the db

    mutable Logger m_logger{createLogger(VerbosityDebug, "chain")};
    mutable Logger m_loggerDetail{createLogger(VerbosityTrace, "chain")};
    mutable Logger m_loggerWarn{createLogger(VerbosityWarning, "chain")};
    mutable Logger m_loggerInfo{createLogger(VerbosityInfo, "chain")};
    mutable Logger m_loggerError{createLogger(VerbosityError, "chain")};

    friend std::ostream& operator<<(std::ostream& _out, BlockChain const& _bc);
};

std::ostream& operator<<(std::ostream& _out, BlockChain const& _bc);

}
}
