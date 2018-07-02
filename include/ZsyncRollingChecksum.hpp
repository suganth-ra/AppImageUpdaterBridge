#ifndef ZSYNC_ROLLING_CHECKSUM_HPP_INCLUDED
#define ZSYNC_ROLLING_CHECKSUM_HPP_INCLUDED
#include <QFuture>
#include <QtConcurrentMap>
#include <QtCore>
#include <ZsyncInternalStructures.hpp>

namespace AppImageUpdaterBridge
{
class ZsyncRollingChecksum : public QObject
{
    Q_OBJECT
public:
    qint64 call_count = 0;
    explicit ZsyncRollingChecksum(QObject *parent = nullptr);
    void setConfiguration(int nblocks,
                          size_t blocksize,
                          unsigned int rsum_bytes,
                          unsigned int checksum_bytes,
                          unsigned int seq_matches,
                          const QString &filename);

    ~ZsyncRollingChecksum();

public Q_SLOTS:
    quint64 getStrongHit(){
	    return _nStrongHit;
    }
    int submitSourceFile(/*QFile*/FILE *file);
    QVector<QPair<zs_blockid, zs_blockid>> neededBlockRanges(zs_blockid from, zs_blockid to);
    void addTargetBlock(zs_blockid b, rsum r, void *checksum);
private Q_SLOTS:
    rsum __attribute__((pure)) calculateRollingChecksum(const unsigned char *data, size_t len);
    void calculateStrongChecksum(unsigned char *buffer, const unsigned char *data, size_t len);
    void writeBlocks(const unsigned char *data,  zs_blockid bfrom, zs_blockid bto);
    ssize_t readKnownData(unsigned char *buffer, off_t offset, size_t len);
    void removeBlockFromHash(zs_blockid id);
    void addToRanges(zs_blockid id);
    zs_blockid nextKnownBlock(zs_blockid x);
    unsigned calcRHash(const hash_entry *const e);
    int rangeBeforeBlock(zs_blockid x);
    int checkChecksumOnHashChain(const hash_entry *e, const unsigned char *data, int onlyone);
    int buildHash(void);

    int submitBlocks(const unsigned char *data, zs_blockid bfrom, zs_blockid bto);
    int submitSourceData(unsigned char *data, size_t len, off_t offset);

    zs_blockid getHashEntryBlockID(const hash_entry *e);
private:
    QMutex _pMutex;
    QSharedPointer<QCryptographicHash> StrongHasher = nullptr;
    rsum _pCurrentSums[2] = { {0, 0}, {0, 0} };    /* current rsums */
    zs_blockid _nBlocks = 0; /* number of blocks in the target file */
    size_t _nBlockSize = 0; /* bytes per block */
    int _nBlockShift = 0; /* log2(blocksize) */
    unsigned short _nRsumMaskA = 0;
    unsigned short _nRsumBits = 0;
    unsigned short _nRsumBytes = 0;
    unsigned _nHashFuncShift = 0; /* config for the hash function */
    unsigned int _nChecksumBytes = 0; /* length of the MD4 Checksum available */
    int _nSeqMatches = 0;
    unsigned int _nContext = 0; /* precalculated blocksize * seq_matches */

    int _nSkip = 0;
    const hash_entry *_pRover = NULL;

    const hash_entry *_pNextMatch = NULL;
    zs_blockid _nNextKnown = 0;

    unsigned int _nHashMask;
    QSharedPointer<hash_entry> _pBlockHashes;
    hash_entry **_pRsumHash = nullptr;

    unsigned char *_cBitHash = NULL;
    unsigned int _nBitHashMask = 0;

    int _nNumRanges = 0;
    QSharedPointer<zs_blockid> _pRanges = NULL;
    int _nGotBlocks = 0;

    QSharedPointer<QFile> _pTargetFile;
    /* Stats */
    quint64 _nHashHit = 0,
            _nWeakHit = 0,
            _nStrongHit = 0,
            _nCheckSummed = 0;
};
}
#endif // ZSYNC_ROLLING_CHECKSUM_HPP_INCLUDED