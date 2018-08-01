#include <AppImageUpdateResource_p.hpp>

using namespace AppImageUpdaterBridge;

/*
 * Prints to the log.
 * LOGS,LOGE  -> Prints normal log messages.
 * INFO_START,INFO_END -> Prints info messages to log.
 * WARNING_START,WARNING_END -> Prints warning messages to log.
 * FATAL_START,FATAL_END -> Prints fatal messages to log.
 *
 * Example:
 * 	LOGS "This is a log message." LOGE
 *
 *
*/
#ifndef LOGGING_DISABLED
#define LOGS *(_pLogger.data()) <<
#define LOGR <<
#define LOGE ; \
	     emit(logger(_sLogBuffer , _sAppImagePath)); \
	     _sLogBuffer.clear();
#else
#define LOGS (void)
#define LOGR ;(void)
#define LOGE ;
#endif // LOGGING_DISABLED

#define INFO_START LOGS "   INFO: " LOGR
#define INFO_END LOGE

#define WARNING_START LOGS "WARNING: " LOGR
#define WARNING_END LOGE

#define FATAL_START LOGS "  FATAL: " LOGR
#define FATAL_END LOGE


/*
 * Macros used for readbility and to reduce repeated
 * code in the source.
 *
 * Warning:
 *     Hardcoded , Do not use this outside AppImageUpdateResourcePrivate
 *     class.
 *
 * Example:
 * 	MEMORY_ERROR()
*/
#define ERROR_STATE
#define MEMORY_ERROR() emit(error(NOT_ENOUGH_MEMORY)); ERROR_STATE
#define APPIMAGE_OPEN_ERROR() emit(error(CANNOT_OPEN_APPIMAGE)); ERROR_STATE
#define APPIMAGE_PERMISSION_ERROR() emit(error(NO_READ_PERMISSION)); ERROR_STATE
#define APPIMAGE_NOT_FOUND_ERROR() emit(error(APPIMAGE_NOT_FOUND)); ERROR_STATE
#define APPIMAGE_READ_ERROR() emit(error(APPIMAGE_NOT_READABLE)); ERROR_STATE
#define APPIMAGE_INVALID_UI_ERROR() emit(error(INVALID_UPDATE_INFORMATION)); ERROR_STATE
#define APPIMAGE_EMPTY_UI_ERROR() emit(error(EMPTY_UPDATE_INFORMATION)); ERROR_STATE
#define MAGIC_BYTES_ERROR() emit(error(INVALID_MAGIC_BYTES)); ERROR_STATE
#define ELF_FORMAT_ERROR() emit(error(UNSUPPORTED_ELF_FORMAT)); ERROR_STATE
#define SECTION_HEADER_NOT_FOUND_ERROR() emit(error(SECTION_HEADER_NOT_FOUND)); ERROR_STATE
#define APPIMAGE_TYPE_ERROR() emit(error(INVALID_APPIMAGE_TYPE)); ERROR_STATE
#define UNSUPPORTED_TRANSPORT_ERROR() emit(error(UNSUPPORTED_TRANSPORT)); ERROR_STATE


/*
 * Returns true if the magic byte is typeX AppImage.
 *
 * Warning:
 * 	This is hardcoded and thus the variable here corresponds
 * 	to the local variable declared in AppImageUpdateResourcePrivate::getInfo
 *
*/
#define TYPE_1_APPIMAGE ((int)magicBytes[2] == 1)
#define TYPE_2_APPIMAGE ((int)magicBytes[2] == 2)


/*
 * Returns true if the given binary is a ElfXX executable.
 *
 * Warning:
 * 	This is hardcoded and thus the variable here corresponds
 * 	to the local variable declared in AppImageUpdateResourcePrivate::getInfo
 *
*/
#define isElf32(data) (((Elf32_Ehdr*)data)->e_ident[EI_CLASS] == ELFCLASS32)
#define isElf64(data) (((Elf64_Ehdr*)data)->e_ident[EI_CLASS] == ELFCLASS64)

/*
 * Sets the offset and length of the need section header
 * from a elf file.
 *
 * Warning:
 * 	Heavily hardcoded , Do not use this outside of AppImageUpdateResourcePrivate::getInfo.
 *
 * Example:
 * 	long unsigned offset = 0 , length = 0;
 *      ElfXX_Ehdr *elfXX = (ElfXX_Ehdr *) data;
 *      ElfXX_Shdr *shdrXX = (ElfXX_Shdr *) (data + elfXX->e_shoff);
 *      strTab = (char *)(data + shdrXX[elfXX->e_shstrndx].sh_offset);
 *
 *      lookupSectionHeaders(strTab , shdr , elf , ".section_header_name");
 *
 *      // Now use offset and length (does nothing if the header is not found).
 */
#define lookupSectionHeaders(strTab , shdr , elf , section) for(int i = 0; i < elf->e_shnum; i++) { \
						  emit(progress((int)((i * 100)/elf->e_shnum))); \
						  if(!strcmp(&strTab[shdr[i].sh_name] , section)){ \
							  offset = shdr[i].sh_offset; \
							  length = shdr[i].sh_size; \
							  emit(progress(80)); \
							  break; \
						  } \
						  }


/*
 * Since the constructor uses this code more often and
 * only the type of 'x' is unique we can use a simple
 * macro to reduce the number of lines in the source.
 *
 * Example:
 * 	CONSTRUCT(QFile *) or
 * 	CONSTRUCT(QString&) or
 * 	CONSTRUCT(nullptr)
*/
#define CONSTRUCT(x) try { \
 		     _pLogger = QSharedPointer<QDebug>(new QDebug(&_sLogBuffer)); \
		     } catch ( ... ) { \
 		     MEMORY_ERROR(); \
 		     throw; \
 		     } \
		     setAppImage(x);


/*
 * Returns a new QByteArray which contains the contents from
 * the given QFile from the given offset to the given
 * max count.
 * This function does not change the position of the QFile.
 *
 * Example:
 * 	QFile file("Some.AppImage")
 * 	file.open(QIODevice::ReadOnly);
 * 	QByteArray data = read(&file , 512 , 1024);
*/
static QByteArray read(QSharedPointer<QFile> IO , qint64 offset, qint64 max)
{
    QByteArray ret;
    qint64 before = IO->pos();
    IO->seek(offset);
    ret = IO->read(max);
    IO->seek(before);
    return ret;
}

/*
 * A Dummy destructor for QFile* , used with smart
 * pointers where the QFile* is not intended to be
 * deleted when dereferenced. Useful when QSharedPointer
 * has to be used as a QWeakPointer which is required
 * in setAppImage(QFile*).
 *
 * Example:
 *      QFile *file = new QFile;
 * 	QSharedPointer<QFile> smartPointer = QSharedPointer<QFile>(file , doNotDelete);
 * 	smartPointer.clear();
 *
 * 	file->setFileName("File.txt"); // Can be reused by the user.
 * 	delete file; // But has to deallocated by the user.
*/
static void doNotDelete(QFile *file)
{  
    /*
     * The variable is not voided for a strong reason.
     * i.e, If the given QFile pointer is actually allocated on
     * the stack , Then voiding it can actually take it off the stack.
     * 
     * Example:
     * 	   QFile file;
     * 	   doNotDelete(&file);
     * 	   // double free error.
    */
    file;
    return;
}


/*
 * AppImageUpdateResourcePrivate is the worker class that provides the
 * ability to easily get the update information from an AppImage.
 * This class can be constructed in three ways.
 * The default construct sets the QObject parent to be null and
 * creates an empty AppImageUpdateResourcePrivate Object.
 *
 * Example:
 * 	QObject parent;
 * 	AppImageUpdateResourcePrivate AppImageInfo(&parent);
 *	// or
 *	AppImageUpdateResourcePrivate AppImageInfo;
*/
AppImageUpdateResourcePrivate::AppImageUpdateResourcePrivate(QObject *parent)
    : QObject(parent)
{
    try {
 	_pLogger = QSharedPointer<QDebug>(new QDebug(&_sLogBuffer));
    } catch ( ... ) {
 	MEMORY_ERROR();
 	throw;
    } 
    return;
}

/*
 * Destructs the AppImageUpdateResourcePrivate ,
 * When the user provides the AppImage as a QFile ,
 * QFile is not closed , the user is fully responsible
 * to deallocate or close the QFile.
*/
AppImageUpdateResourcePrivate::~AppImageUpdateResourcePrivate()
{
    _pLogger.clear();
    _pAppImage.clear();
    return;
}

void AppImageUpdateResourcePrivate::setLoggerName(const QString &name)
{
	_sLoggerName = QString(name);
	return;
}

/*
 * This method returns nothing and sets the AppImage
 * referenced by the given QString , The QString is expected to be a
 * valid path either an absolute or a relative one.
 * if the path is empty then this exits doing nothing.
 * if the path specified does not exist then this will fire
 * the error signal.
 *
 * Example:
 * 	AppImageUpdateResourcePrivate AppImageInfo;
 * 	AppImageInfo.setAppImage("PathTo.AppImage");
 *
 */
void AppImageUpdateResourcePrivate::setAppImage(const QString &AppImagePath)
{
    clear(); /* clear old data */
    if(AppImagePath.isEmpty()) {
        WARNING_START  " setAppImage : AppImagePath is empty , operation ignored." WARNING_END;
        return;
    }

   /*
    * Set the AppImage Name as a nickname for identification
    * on the logger.
    */
    _sAppImagePath = AppImagePath;
    _sAppImageName = QFileInfo(AppImagePath).fileName();

    try {
        _pAppImage = QSharedPointer<QFile>(new QFile);
    } catch ( ... ) {
        MEMORY_ERROR();
        throw;
    }


    INFO_START  " setAppImage : " LOGR AppImagePath LOGR "." INFO_END;

    _pAppImage->setFileName(AppImagePath);

    /* Check if the file actually exists. */
    if(!_pAppImage->exists()) {
        _pAppImage.clear(); /* delete everything to avoid futher errors. */
        FATAL_START  " setAppImage : cannot find the AppImage in the given path , file not found." FATAL_END;
        APPIMAGE_NOT_FOUND_ERROR();
        return;
    }

    /* Check if we have the permission to read it. */
    auto perm = _pAppImage->permissions();
    if(
        !(perm & QFileDevice::ReadUser) &&
        !(perm & QFileDevice::ReadGroup) &&
        !(perm & QFileDevice::ReadOther)
    ) {
        _pAppImage.clear();
        FATAL_START  " setAppImage : no permission(" LOGR perm LOGR ") for reading the given AppImage." FATAL_END;
        APPIMAGE_PERMISSION_ERROR();
        return;
    }

    /*
     * Finally open the file.
    */
    if(!_pAppImage->open(QIODevice::ReadOnly)) {
        _pAppImage.clear();
        FATAL_START  " setAppImage : cannot open AppImage for reading." FATAL_END;
        APPIMAGE_OPEN_ERROR();
        return;
    }

    return;
}


/*
 * This is a overloaded method , Sets the AppImage with reference
 * to the given QFile pointer , The given QFile has to be opened and
 * must be readable.
 *
 * Example:
 * 	AppImageUpdateResourcePrivate AppImageInfo;
 * 	QFile file("PathTo.AppImage");
 * 	file.open(QIODevice::ReadOnly);
 * 	AppImageInfo.setAppImage(&file);
 * 	file.close();
*/
void AppImageUpdateResourcePrivate::setAppImage(QFile *AppImage)
{
    clear(); /* clear old data. */
    if(AppImage == nullptr) {
        WARNING_START " setAppImage : given AppImage QFile is nullptr , operation ignored. " WARNING_END
        return;
    }

    /* Set nickname and cache path. */
    _sAppImagePath = AppImage->fileName();
    _sAppImageName = QFileInfo(_sAppImagePath).fileName();

    _pAppImage = QSharedPointer<QFile>(AppImage, doNotDelete);

    INFO_START  " setAppImage : " LOGR _pAppImage->fileName() LOGR "." INFO_END;

    /* Check if exists */
    if(!_pAppImage->exists()) {
        _pAppImage.clear();
        FATAL_START  " setAppImage : cannot find the AppImage from given QFile , file does not exists." FATAL_END;
        APPIMAGE_NOT_FOUND_ERROR();
        return;
    }

    /* Check if readable. */
    if(!_pAppImage->isReadable()) {
        _pAppImage.clear(); /* delete everything */
        FATAL_START  " setAppImage : invalid QFile given, not readable." FATAL_END;
        APPIMAGE_READ_ERROR();
        return;
    }

    /* Check if opened. */
    if(!_pAppImage->isOpen()) {
        _pAppImage.clear(); /* delete everything */
        FATAL_START  " setAppImage : invalid QFile given, not opened." FATAL_END;
        APPIMAGE_OPEN_ERROR();
        return;
    }

    return;
}

/*
 * This returns the caller object , If the given bool
 * is true then it connects the logger signal to the
 * logPrinter slot to enable debugging messages.
 * On false this disconnects the logPrinter.
 *
 * Example:
 * 	AppImageUpdateResourcePrivate AppImageInfo("PathTo.AppImage");
 * 	AppImageInfo.setShowLog(true);
*/
void AppImageUpdateResourcePrivate::setShowLog(bool logNeeded)
{
    if(logNeeded) {
        disconnect(this, &AppImageUpdateResourcePrivate::logger, this, &AppImageUpdateResourcePrivate::logPrinter);
        connect(this, &AppImageUpdateResourcePrivate::logger, this, &AppImageUpdateResourcePrivate::logPrinter);
        INFO_START  " setShowLog : true  , started logging." INFO_END;

    } else {
        INFO_START  " setShowLog : false , finishing logging." INFO_END;
        disconnect(this, &AppImageUpdateResourcePrivate::logger, this, &AppImageUpdateResourcePrivate::logPrinter);
    }
    return;
}


/*
 * This returns the copy to the data held inside
 * a QJsonObject privately. If this is empty then
 * that means the user never started anything.
 *
 * Example:
 * 	QJsonObject info = AppImageInfo.getInfo();
*/
void AppImageUpdateResourcePrivate::getInfo(void)
{
    /*
     * If this class is constructed without a 
     * AppImage to operate on ,Then lets guess it.
    */
    if(_pAppImage.isNull()){
    /*
     * Check if QCoreApplication got something on argv[0].
     * The main payload is not the one we want to operate this on 
     * but the AppImage itself , So we cannot use the actual application executable
     * path processed by qt but argv[0].
     *
     * Only with argv[0] we cannot determine the path of the actual AppImage
     * so we will also need the application directory path which is also 
     * available by QCoreApplication.
     *
     * Therefore ,
     *     AppImagePath = Application Directory + "/" + filename in argv[0].
     *
     * Note: We don't need to use a native seperator since Qt itself will manage 
     * that if we use '/' , Also AppImage is exclusive for linux and thus '/' is 
     * default for any linux filesystem even if not so , Qt will handle it.
    */
    auto arguments = QCoreApplication::arguments();
    if(!arguments.isEmpty()){
    	setAppImage(QCoreApplication::applicationDirPath() + 
		    "/" + 
		    QFileInfo(arguments.at(0)).fileName());
    }
    }

    /*
     * Check if the user called this twice ,
     * If so , We don't need to waste our time on
     * calculating the obvious.
     * Note: _jInfo will always will be empty for a new
     * AppImage , And so if it is not empty then that implies
     * that the user called getInfo() twice or more.
     */
    if(!_jInfo.isEmpty()){
	    emit(info(_jInfo));
	    return;
    }
    
    QString AppImageSHA1;
    QString updateString;
    QStringList data;

    /*
     * Calculate the AppImages SHA1 Hash which will be 
     * used later to find if we need to update the 
     * AppImage.
    */
    {
    qint64 bufferSize = 0;
    if(_pAppImage->size() >= 1073741824){ // 1 GiB and more.
	    bufferSize = 104857600; // copy per 100 MiB.
    }
    else if(_pAppImage->size() >= 1048576 ){ // 1 MiB and more.
	    bufferSize = 1048576; // copy per 1 MiB.
    }else if(_pAppImage->size() >= 1024){ // 1 KiB and more.
	    bufferSize = 4096; // copy per 4 KiB.
    }else{ // less than 1 KiB
	    bufferSize = 1024; // copy per 1 KiB.
    }

    QCryptographicHash *SHA1Hasher = new QCryptographicHash(QCryptographicHash::Sha1);
    while(!_pAppImage->atEnd()){
	SHA1Hasher->addData(_pAppImage->read(bufferSize));
    }
    _pAppImage->seek(0); // rewind file to the top for later use.
    AppImageSHA1 = QString(SHA1Hasher->result().toHex().toUpper());	
    delete SHA1Hasher; 
    }

    /*
     * Read the magic byte , i.e the AI stamp
     * on the given binary. The characters 'AI'
     * are hardcoded at the offset 8 with a
     * maximum of 3 characters.
     * The 3rd character decides the type of the
     * AppImage.
    */
    auto magicBytes = read(_pAppImage, /*offset=*/8,/*maxchars=*/ 3);
    if (magicBytes[0] != 'A' && magicBytes[1] != 'I') {
        FATAL_START  " getInfo : invalid magic bytes("
                    LOGR (unsigned)magicBytes[0] LOGR ","
                    LOGR (unsigned)magicBytes[1] LOGR ")." FATAL_END;
	MAGIC_BYTES_ERROR();
	return;
    }

    /*
     * 0x1H -> Type 1 AppImage.
     * 0x2H -> Type 2 AppImage. (Latest Version)
    */
    if(TYPE_1_APPIMAGE) {

        INFO_START  " getInfo : AppImage is confirmed to be type 1." INFO_END;

        progress(/*percentage=*/80); /*Signal progress.*/
        updateString = QString::fromUtf8(read(_pAppImage, APPIMAGE_TYPE1_UPDATE_INFO_POS, APPIMAGE_TYPE1_UPDATE_INFO_LEN));

    } else if(TYPE_2_APPIMAGE) {

        INFO_START  " getInfo : AppImage is confirmed to be type 2." INFO_END;
        INFO_START  " getInfo : mapping AppImage to memory." INFO_END;

        {
            uint8_t *data = NULL;
            char *strTab = NULL;
            uchar *mapped = NULL;
            unsigned long offset = 0, length = 0;

            mapped = _pAppImage->map(/*offset=*/0, /*max=*/_pAppImage->size()); // mmap in Qt.

            if(mapped == NULL) {
                FATAL_START  " getInfo : not enough memory to map AppImage to memory." FATAL_END;
                MEMORY_ERROR();
                return;
            }

            data = (uint8_t*) mapped;
            if(isElf32(data)) {
                INFO_START  " getInfo : AppImage architecture is x86 (32 bits)." INFO_END;

                Elf32_Ehdr *elf32 = (Elf32_Ehdr *) data;
                Elf32_Shdr *shdr32 = (Elf32_Shdr *) (data + elf32->e_shoff);

                strTab = (char *)(data + shdr32[elf32->e_shstrndx].sh_offset);
                lookupSectionHeaders(strTab, shdr32, elf32, APPIMAGE_TYPE2_UPDATE_INFO_SHDR);
            } else if(isElf64(data)) {
                INFO_START  " getInfo : AppImage architecture is x86_64 (64 bits)." INFO_END;

                Elf64_Ehdr *elf64 = (Elf64_Ehdr *) data;
                Elf64_Shdr *shdr64 = (Elf64_Shdr *) (data + elf64->e_shoff);

                strTab = (char *)(data + shdr64[elf64->e_shstrndx].sh_offset);
                lookupSectionHeaders(strTab, shdr64, elf64, APPIMAGE_TYPE2_UPDATE_INFO_SHDR);
            } else {
                _pAppImage->unmap(mapped);
                FATAL_START  " getInfo : Unsupported elf format." FATAL_END;
                ELF_FORMAT_ERROR();
                return;
            }

            _pAppImage->unmap(mapped); // equivalent to unmap.

            if(offset == 0 || length == 0) {
                FATAL_START  " getInfo : cannot find '"
                    	     LOGR APPIMAGE_TYPE2_UPDATE_INFO_SHDR LOGR "' section header." FATAL_END;
                SECTION_HEADER_NOT_FOUND_ERROR();
            } else {
                updateString = QString::fromUtf8(read(_pAppImage, offset, length));
            }
        }
    } else {
        WARNING_START  " getInfo : unable to confirm AppImage type." WARNING_END;
        if(
            (read(_pAppImage, ELF_MAGIC_POS, ELF_MAGIC_VALUE_SIZE) == ELF_MAGIC_VALUE) &&
            (read(_pAppImage, ISO_MAGIC_POS, ISO_MAGIC_VALUE_SIZE) == ISO_MAGIC_VALUE)
        ) {
            WARNING_START  " getInfo : guessing AppImage type to be 1." WARNING_END;
            emit(progress(80));
            updateString = QString::fromUtf8(read(_pAppImage, APPIMAGE_TYPE1_UPDATE_INFO_POS, APPIMAGE_TYPE1_UPDATE_INFO_LEN));
        } else {
            FATAL_START  " getInfo : invalid AppImage type(" LOGR (unsigned)magicBytes[2] LOGR ")." FATAL_END;
            APPIMAGE_TYPE_ERROR();
            return;
        }
    }

    if(updateString.isEmpty()) {
        FATAL_START  " getInfo : update information is empty." FATAL_END;
        APPIMAGE_EMPTY_UI_ERROR();
        return;
    }

    INFO_START " getInfo : updateString(" LOGR updateString LOGR ")." INFO_END;

    /*
     * Split the raw update information with the specified
     * delimiter.
    */
    data = updateString.split(APPIMAGE_UPDATE_INFO_DELIMITER);

    // This will be sent along the update information.
    QJsonObject fileInformation {
	{ "AppImageFilePath" , _sAppImagePath },
	{ "AppImageSHA1Hash" , AppImageSHA1 }
    };

    QJsonObject updateInformation; // will be filled up later on.

    if(data.size() < 2) {
        FATAL_START  " getInfo : update information has invalid delimiters." FATAL_END;
        APPIMAGE_INVALID_UI_ERROR();
        return;
    } else if(data.size() == 2) {
	{
	QJsonObject buffer {
            { "transport", data.at(0) },
            { "zsyncUrl", data.at(1) }
        };
        updateInformation = buffer;
        } 
   } else if(data.size() == 5) {
        if(data.at(0) == "gh-releases-zsync") {
	    {
	    QJsonObject buffer {
                {"transport", data.at(0) },
                {"username", data.at(1) },
                {"repo", data.at(2) },
                {"tag", data.at(3) },
                {"filename", data.at(4) }
            };
            updateInformation = buffer;
            }
	} else if(data.at(0) == "bintray-zsync") {
	    {
	    QJsonObject buffer {
                {"transport", data.at(0) },
                {"username", data.at(1) },
                {"repo", data.at(2) },
                {"packageName", data.at(3) },
                {"filename", data.at(4) }
            };
            updateInformation = buffer;
            }
	} else {
            FATAL_START  " getInfo : unsupported transport mechanism given." FATAL_END;
            UNSUPPORTED_TRANSPORT_ERROR();
            return;
        }

    } else {
        FATAL_START " getInfo : update information has invalid number of entries(" LOGR data.size() LOGR ")." FATAL_END;
        APPIMAGE_INVALID_UI_ERROR();
        return;
    }

    {
    QJsonObject buffer {
	{ "IsEmpty" , updateInformation.isEmpty() },
	{ "FileInformation" , fileInformation } ,
	{ "UpdateInformation" , updateInformation }
    };
    _jInfo = buffer;
    }

    emit(progress(100)); /*Signal progress.*/

    emit(info(_jInfo));
    INFO_START  " getInfo : finished." INFO_END;
    return;
}

/*
 * This returns the caller object , This clears all
 * the data held in the current object , making it
 * reusable.
 *
 * Example:
 * 	AppImageInfo.clear();
*/
void AppImageUpdateResourcePrivate::clear(void)
{
    _jInfo = QJsonObject();
    _sLogBuffer.clear();
    _sAppImagePath.clear();
    _sAppImageName.clear();
    _pAppImage.clear();
    INFO_START  " clear : flushed everything." INFO_END;
    return;
}

/* This private slot proxies the log messages from
 * the logger signal to qDebug().
*/
void AppImageUpdateResourcePrivate::logPrinter(QString msg , QString path)
{
    (void)path;
    qDebug().noquote() << "["
                       <<  QDateTime::currentDateTime().toString(Qt::ISODate)
                       << " | "
		       <<  QThread::currentThreadId()
		       << "] "
		       << _sLoggerName
		       << "("
                       << _sAppImageName << ")::" << msg;
    return;
}

/*
 * This static method returns a QString which corresponds the
 * AppImageUpdateResourcePrivate::error_code , Useful when logging and debuging.
 *
 * Example:
 * 	qDebug()
 * 	<< AppImageUpdateResourcePrivate::errorCodeToString(AppImageUpdateResourcePrivate::APPIMAGE_NOT_FOUND);
*/
QString AppImageUpdateResourcePrivate::errorCodeToString(short errorCode)
{
    QString ret = "AppImageUpdateInformation::errorCode(";
    switch(errorCode) {
    case 0:
        ret += "APPIMAGE_NOT_READABLE)";
        break;
    case 1:
        ret += "NO_READ_PERMISSION)";
        break;
    case 2:
        ret += "APPIMAGE_NOT_FOUND)";
        break;
    case 3:
        ret += "CANNOT_OPEN_APPIMAGE)";
        break;
    case 4:
        ret += "EMPTY_UPDATE_INFORMATION)";
        break;
    case 5:
        ret += "INVALID_APPIMAGE_TYPE)";
        break;
    case 6:
        ret += "INVALID_MAGIC_BYTES)";
        break;
    case 7:
        ret += "INVALID_UPDATE_INFORMATION)";
        break;
    case 8:
        ret += "NOT_ENOUGH_MEMORY)";
        break;
    case 9:
        ret += "SECTION_HEADER_NOT_FOUND)";
        break;
    case 10:
        ret += "UNSUPPORTED_ELF_FORMAT)";
        break;
    case 11:
        ret += "UNSUPPORTED_TRANSPORT)";
        break;
    default:
        ret += "Unknown)";
        break;
    }
    return ret;
}
