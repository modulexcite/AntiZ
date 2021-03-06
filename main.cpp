#include <fstream>
#include <vector>
#include <cstring>//for memset()
#include <zlib.h>
#include <tclap/CmdLine.h>
#define antiz_ver "0.1.5-git"

namespace ATZutil{
    void copyto(std::ofstream& outfile, const std::string ifname, const uint64_t length, const uint64_t inoffset, const uint64_t chunksize);
    inline int getFilesize(const std::string fname, uint64_t& fsize){
        std::ifstream f;
        f.open(fname, std::ios::in | std::ios::binary);//open the file and check for error
        if (!f.is_open()){
           std::cout<< "error: open file for size check failed!" <<std::endl;
           return -1;
        }
        f.seekg (0, f.end);//getting the size of the file
        fsize=f.tellg();
        f.seekg (0, f.beg);
        f.close();
        return 0;
    }
    inline void pauser_debug(){
        #ifdef debug
            std::string dummy;
            std::cout << "Press enter to continue...";
            std::getline(std::cin, dummy);
        #endif // debug
    }
    class inbuffer{
    public:
        inbuffer()=delete;//the default constructor should not be used in this version
        inbuffer(std::string fname, uint64_t bs, uint64_t sp){
            buffsize=bs;
            startpos=sp;
            f.open(fname, std::ios::in | std::ios::binary);//open the input file
            if(f.is_open()){
                open=true;
                buff=new unsigned char[buffsize];
                f.seekg(startpos);
                buffstart=startpos;
                f.read(reinterpret_cast<char*>(buff), buffsize);
                #ifdef debug
                nreads++;
                #endif // debug
            }
        }
        ~inbuffer(){
            delete [] buff;
            f.close();
            #ifdef debug
                std::cout<<"Total reads: "<<nreads<<std::endl;
            #endif // debug
        }
        void next_chunk(){
            buffstart=buffstart+buffsize;
            f.read(reinterpret_cast<char*>(buff), buffsize);
            #ifdef debug
                nreads++;
            #endif // debug
        }
        void restart(){
            f.clear();
            f.seekg(startpos);
            buffstart=startpos;
            f.read(reinterpret_cast<char*>(buff), buffsize);
            #ifdef debug
                nreads++;
            #endif // debug
        }
        void seekread(uint64_t pos){
            f.clear();
            f.seekg(pos);
            buffstart=pos;
            f.read(reinterpret_cast<char*>(buff), buffsize);
            #ifdef debug
                nreads++;
            #endif // debug
        }
        void seekread_rel(int64_t relpos){
            f.clear();
            f.seekg(relpos, f.cur);
            buffstart=(int64_t)buffstart+relpos;
            f.read(reinterpret_cast<char*>(buff), buffsize);
            #ifdef debug
                nreads++;
            #endif // debug
        }
        bool eof(){
            return f.eof();
        }
        unsigned char* buff;
        bool open=false;
        uint64_t buffstart;
    private: //private section of inbuffer
        std::ifstream f;
        uint64_t startpos,buffsize;
        #ifdef debug
            uint64_t nreads=0;
        #endif // debug
    };
    void copyto(std::ofstream& outfile, const std::string ifname, const uint64_t length, const uint64_t inoffset, const uint64_t chunksize){
        if (chunksize>=length){
            inbuffer buffobj(ifname, length, inoffset);
            outfile.write(reinterpret_cast<char*>(buffobj.buff), length);
        }else{
            inbuffer buffobj(ifname, chunksize, inoffset);
            uint64_t done=0;
            while(done<=(length-chunksize)){
                outfile.write(reinterpret_cast<char*>(buffobj.buff), chunksize);
                buffobj.next_chunk();
                done=done+chunksize;
            }
            if ((length-done)!=0){
                outfile.write(reinterpret_cast<char*>(buffobj.buff), length-done);
            }
        }
    }
    inline void read2buff(const std::string fname, unsigned char buff[], const uint64_t bufflen, const uint64_t offset){
        std::ifstream infile;
        infile.open(fname, std::ios::in | std::ios::binary);
        infile.seekg(offset);
        infile.read(reinterpret_cast<char*>(buff), bufflen);
        infile.close();
    }
}

namespace ATZdata{
    struct programOptions{
        //parameters that some users may want to tweak
        uint_fast16_t recompTresh;//streams are only recompressed if the best match differs from the original in <= recompTresh bytes
        uint_fast16_t sizediffTresh;//streams are only compared when the size difference is <= sizediffTresh
        uint_fast16_t shortcutLength;//stop compression and count mismatches after this many bytes, if we get more than recompTresh then bail early
        uint_fast16_t mismatchTol;//if there are at most this many mismatches consider the stream a full match and stop looking for better parameters
        bool bruteforceWindow=false;//bruteforce the zlib parameters, otherwise only try probable parameters based on the 2-byte header
        uint64_t chunksize;//the size of buffers used for file IO, controls memory usage

        //debug parameters, not useful for most users
        bool shortcutEnabled=true;//enable speedup shortcut in phase 3
        int_fast64_t concentrate=-1;//only try to recompress the stream# givel here, negative values disable this and run on all streams, debug tool

        //command line switches
        bool recon;
        bool notest;
    };
    class streamOffset{
    public:
        streamOffset()=delete;//the default constructor should not be used in this version
        streamOffset(uint64_t os, int ot, uint64_t sl, uint64_t il){
            offset=os;
            offsetType=ot;
            streamLength=sl;
            inflatedLength=il;
            clevel=9;
            window=15;
            memlvl=9;
            identBytes=0;
            firstDiffByte=-1;
            recomp=false;
            atzInfos=0;
        }
        uint64_t offset;
        int offsetType;
        uint64_t streamLength;
        uint64_t inflatedLength;
        uint8_t clevel;
        uint8_t window;
        uint8_t memlvl;
        uint64_t identBytes;
        int_fast64_t firstDiffByte;//the offset of the first byte that does not match, relative to stream start, not file start
        std::vector<uint64_t> diffByteOffsets;//offsets of bytes that differ, this is an incremental offset list to enhance recompression, kinda like a PNG filter
        //this improves compression if the mismatching bytes are consecutive, eg. 451,452,453,...(no repetitions, hard to compress)
        //  transforms into 0, 1, 1, 1,...(repetitive, easy to compress)
        std::vector<uint8_t> diffByteVal;
        bool recomp;
        uint64_t atzInfos;
    };
    class fileOffset{
    public:
        fileOffset()=delete;//the default constructor should not be used in this version
        fileOffset(uint64_t os, int ot){
            offset=os;
            offsetType=ot;
        }
        uint64_t offset;
        int offsetType;
    };
}

class ATZcreator{
public:
    ATZcreator()=delete;
    ATZcreator(std::string ifname, std::string atzname, std::string recname, ATZdata::programOptions opt){
        infileName=ifname;
        atzfileName=atzname;
        reconfileName=recname;
        options=opt;
        infileSize=0;
        processingState=0;
    }
    int Phase1(){
        //PHASE 1
        //search the file for zlib headers, count them and create an offset list
        if (processingState!=0) return -10;
        if (ATZutil::getFilesize(infileName, infileSize)!=0) return -1;//if opening the file fails, exit
        std::cout<<"Input file size:"<<infileSize<<std::endl;
        //try to guess the number of potential zlib headers in the file from the file size
        //this value is purely empirical, may need tweaking
        fileOffsetList.reserve(static_cast<uint64_t>(infileSize/1912));
        #ifdef debug
            std::cout<<"Offset list initial capacity:"<<fileOffsetList.capacity()<<std::endl;
        #endif
        if (infileSize>options.chunksize){
            searchInfile(options.chunksize);//search the file for zlib headers
        }else{
            searchInfile(infileSize+1);//the +1 makes sure we get eof and skip the while loop
        }
        std::cout<<"Total zlib headers found: "<<fileOffsetList.size()<<std::endl;
        processingState=1;
        return 0;
    }
    int Phase2(){
        //PHASE 2
        //start trying to decompress at the collected offsets, test all offsets found in phase 1
        if (processingState!=1) return -10;
        testOffsetList_chunked(options.chunksize);
        std::cout<<"Valid zlib streams: "<<streamOffsetList.size()<<std::endl;
        fileOffsetList.clear();//we only need the good offsets
        fileOffsetList.shrink_to_fit();
        processingState=2;
        return 0;
    }
    int Phase3(){
        //PHASE 3
        //start trying to find the parameters to use for recompression
        if (processingState!=2) return -10;
        findDeflateParams_ALL();
        std::cout<<std::endl;
        #ifdef debug
            printStreaminfo_ALL(options.mismatchTol);
        #endif // debug
        std::cout<<"recompressed:"<<countRecomp()<<"/"<<streamOffsetList.size()<<std::endl;
        processingState=3;
        return 0;
    }
    int Phase4(){
        //PHASE 4
        //take the information gathered in phase 3 and use it to create an ATZ file
        if (processingState!=3) return -10;
        writeATZfile(infileName, atzfileName, options.chunksize);
        streamOffsetList.clear();
        streamOffsetList.shrink_to_fit();
        processingState=3;
        return 0;
    }
private: //private section of ATZprocess
    std::string infileName;
	std::string atzfileName;
	std::string reconfileName;
    std::vector<ATZdata::fileOffset> fileOffsetList;//offsetList stores memory offsets where potential headers can be found, and the type of the offset
    std::vector<ATZdata::streamOffset> streamOffsetList;//streamOffsetList stores offsets of confirmed zlib streams and a bunch of data on them
	ATZdata::programOptions options;
	int processingState;
	uint64_t infileSize;

    inline uint64_t countRecomp(){
        uint64_t i,nrecomp=0;
        for (i=0; i<streamOffsetList.size(); i++){
            if (streamOffsetList[i].recomp==true) nrecomp++;
        }
        return nrecomp;
    }
    int inflate_f2f(const std::string infile, const std::string outfile, const uint64_t inoffset, const uint64_t chunksize){
        std::ifstream in;
        std::ofstream out;
        z_stream strm;
        unsigned char* inbuff;
        unsigned char* outbuff;
        int ret, ret2;
            //initialize stuff
        inbuff=new unsigned char[chunksize];
        outbuff=new unsigned char[chunksize];
        in.open(infile, std::ios::in | std::ios::binary);//open the input file
        out.open(outfile, std::ios::out | std::ios::binary | std::ios::trunc);//open the output file
        if ((!in.is_open())||(!out.is_open())){//check for error
            in.close();
            out.close();
            delete [] inbuff;
            delete [] outbuff;
            return -1;
        }
        in.seekg(inoffset);//seek to the beginning of the stream
        in.read(reinterpret_cast<char*>(inbuff), chunksize);
        strm.zalloc=Z_NULL;
        strm.zfree=Z_NULL;
        strm.opaque=Z_NULL;
        strm.next_in=inbuff;
        strm.avail_in=chunksize;
        strm.next_out=outbuff;
        strm.avail_out=chunksize;
        if (inflateInit(&strm)!=Z_OK){//initialize the zlib stream
            std::cout<<"inflateInit() failed"<<std::endl;
            abort();
        }
            //do the decompression
        while(true){
            ret=inflate(&strm, Z_FINISH);
            if( ret==Z_STREAM_END){//reached the end of the stream correctly
                ret2=0;
                out.write(reinterpret_cast<char*>(outbuff), (chunksize-strm.avail_out));//write the last output
                break;
            }
            if (ret!=Z_BUF_ERROR){//if there is an error other than running out of a buffer
                std::cout<<"error, zlib returned with unexpected value: "<<ret<<std::endl;
                abort();
            }
            if (strm.avail_out==0){//if we get buf_error and ran out of output, write it to file
                out.write(reinterpret_cast<char*>(outbuff), chunksize);
                strm.next_out=outbuff;//reuse the buffer
                strm.avail_out=chunksize;
            }
            if (strm.avail_in==0){//if we get buf_error and ran out of input, read in the next chunk
                in.read(reinterpret_cast<char*>(inbuff), chunksize);
                strm.next_in=inbuff;
                strm.avail_in=chunksize;
            }
        }
            //clean up and free memory
        in.close();
        out.close();
        if (inflateEnd(&strm)!=Z_OK){
            std::cout<<"inflateEnd() failed"<<std::endl;//should never happen normally
            abort();
        }
        delete [] inbuff;
        delete [] outbuff;
        return ret2;
    }
    void searchInfile(const uint64_t buffsize){
        //open a file and search it for possible Zlib headers
        //all information about them is pushed into a vector
        std::ifstream f;
        uint64_t i;
        unsigned char* rBuffer;
        f.open(infileName, std::ios::in | std::ios::binary);//open the input file
        rBuffer = new unsigned char[buffsize];
        memset(rBuffer, 0, buffsize);
        f.read(reinterpret_cast<char*>(rBuffer), buffsize);
        searchBuffer(rBuffer, buffsize);//do the 0-th chunk
        i=1;
        while (!f.eof()){//read in and process the file until the end of file
            memset(rBuffer, 0, buffsize);//the buffer needs to be zeroed out, or the last chunk will cause a crash
            f.seekg(-1, f.cur);//seek back one byte because the last byte in the previous chunk never gets parsed
            f.read(reinterpret_cast<char*>(rBuffer), buffsize);
            searchBuffer(rBuffer, buffsize, (i*buffsize-i));
            i++;
        }
        f.close();
        delete [] rBuffer;
    }
    void searchBuffer(unsigned char buffer[], const uint64_t buffLen, const uint64_t chunkOffset=0){
        //this function searches a buffer for zlib headers, count them and fill a vector of fileOffsets
        //chunkOffset is used if the input buffer is just a chunk of a bigger set of data (eg. a file that does not fit into RAM)
        //a new variable is used so the substraction is only performed once, not every time it loops
        //it is pointless to test the last byte and it could cause and out of bounds read
        uint64_t redlen=buffLen-1;
        for(uint64_t i=0;i<redlen;i++){
            //search for 7801, 785E, 789C, 78DA, 68DE, 6881, 6843, 6805, 58C3, 5885, 5847, 5809,
            //           48C7, 4889, 484B, 480D, 38CB, 388D, 384F, 3811, 28CF, 2891, 2853, 2815
            int header = ((int)buffer[i]) * 256 + (int)buffer[i + 1];
            int offsetType = parseOffsetType(header);
            if (offsetType >= 0){
                #ifdef debug
                    std::cout << "Zlib header 0x" << std::hex << std::setfill('0') << std::setw(4) << header << std::dec
                        << " with " << (1 << ((header >> 12) - 2)) << "K window at offset: " << (i+chunkOffset) << std::endl;
                #endif // debug
                fileOffsetList.push_back(ATZdata::fileOffset(i+chunkOffset, offsetType));
            }
        }
        #ifdef debug
            std::cout<<std::endl;
        #endif // debug
    }
    int parseOffsetType(const int header){
        // A zlib stream has the following structure: (http://tools.ietf.org/html/rfc1950)
        //  +---+---+   CMF: bits 0 to 3  CM      Compression method (8 = deflate)
        //  |CMF|FLG|        bits 4 to 7  CINFO   Compression info (base-2 logarithm of the LZ77 window size minus 8)
        //  +---+---+
        //              FLG: bits 0 to 4  FCHECK  Check bits for CMF and FLG (in MSB order (CMF*256 + FLG) is a multiple of 31)
        //                   bit  5       FDICT   Preset dictionary
        //                   bits 6 to 7  FLEVEL  Compression level (0 = fastest, 1 = fast, 2 = default, 3 = maximum)
        switch (header){
            case 0x2815 : return 0;  case 0x2853 : return 1;  case 0x2891 : return 2;  case 0x28cf : return 3;
            case 0x3811 : return 4;  case 0x384f : return 5;  case 0x388d : return 6;  case 0x38cb : return 7;
            case 0x480d : return 8;  case 0x484b : return 9;  case 0x4889 : return 10; case 0x48c7 : return 11;
            case 0x5809 : return 12; case 0x5847 : return 13; case 0x5885 : return 14; case 0x58c3 : return 15;
            case 0x6805 : return 16; case 0x6843 : return 17; case 0x6881 : return 18; case 0x68de : return 19;
            case 0x7801 : return 20; case 0x785e : return 21; case 0x789c : return 22; case 0x78da : return 23;
            default: return -1;
        }
    }
    void testOffsetList_chunked(const uint64_t buffsize){
        //this function takes a vector of fileOffsets and the name of the file, and tests if the offsets in the fileOffset vector
        //are marking the beginnings of valid zlib streams
        //the offsets, types, lengths and inflated lengths of valid zlib streams are pushed to a vector of streamOffsets
        uint64_t numOffsets=fileOffsetList.size();
        uint64_t lastGoodOffset=0;
        uint64_t lastStreamLength=0;
        uint64_t i;
        int ret;
        z_stream strm;
        strm.zalloc=Z_NULL;
        strm.zfree=Z_NULL;
        strm.opaque=Z_NULL;
        ATZutil::inbuffer infile(infileName, buffsize, 0);//for reading in data from the file
        for (i=0; i<numOffsets; i++){
            if ((lastGoodOffset+lastStreamLength)<=fileOffsetList[i].offset){//if the current offset is known to be part of the last stream it is pointless to check it
                //read in the first chunk
                //since we have no idea about the length of the zlib stream, take the worst case, i.e. everything after the header belongs to the stream
                if ((fileOffsetList[i].offset>=infile.buffstart)&&(fileOffsetList[i].offset<(infile.buffstart+buffsize-15))){//if the first 16 bytes of the current stream is already in the buffer
                    strm.next_in=infile.buff+(fileOffsetList[i].offset-infile.buffstart);
                    strm.avail_in=buffsize-(fileOffsetList[i].offset-infile.buffstart);
                } else {
                    infile.seekread(fileOffsetList[i].offset);//seek to the beginning of the current stream
                    strm.next_in=infile.buff;
                    strm.avail_in=buffsize;
                }
                if (inflateInit(&strm)!=Z_OK){
                    std::cout<<"inflateInit() failed"<<std::endl;
                    abort();
                }
                while(true){
                    ret=CheckOffset_chunked(strm, buffsize);
                    if (ret==0){
                        lastGoodOffset=fileOffsetList[i].offset;
                        lastStreamLength=strm.total_in;
                        streamOffsetList.push_back(ATZdata::streamOffset(fileOffsetList[i].offset, fileOffsetList[i].offsetType, strm.total_in, strm.total_out));
                        #ifdef debug
                        std::cout<<"Offset #"<<i<<" ("<<fileOffsetList[i].offset<<") decompressed, "<<strm.total_in<<" bytes to "<<strm.total_out<<" bytes"<<std::endl;
                        #endif // debug
                        break;
                    }
                    if (ret==1) break;//if the stream is invalid
                    if (ret==2){//need more input
                        if (infile.eof()) break;//if there is a truncated Zlib stream at the end of the file
                        infile.next_chunk();
                        strm.next_in=infile.buff;
                        strm.avail_in=buffsize;
                    }
                    if (ret<0) abort();//should never happen normally
                }
                if (inflateEnd(&strm)!=Z_OK){
                    std::cout<<"inflateEnd() failed"<<std::endl;//should never happen normally
                    abort();
                }
            }
            #ifdef debug
            else{
                std::cout<<"skipping offset #"<<i<<" ("<<fileOffsetList[i].offset<<") because it cannot be a header"<<std::endl;
            }
            #endif // debug
        }
        std::cout<<std::endl;
    }
    inline int CheckOffset_chunked(z_stream& strm, const uint64_t buffsize){
        //this is kinda like a wrapper function for inflate(), to be used for testing zlib streams
        //since we dont need the inflated data, it can be thrown away to limit memory usage
        //strm MUST be already initialized with inflateInit before calling this function
        //strm should be deallocated with inflateEnd when done, to prevent memory leak
        //returns 0 if the stream is valid, 1 if invalid, 2 if the next chunk is needed, negative values for error
        unsigned char* decompBuffer= new unsigned char[buffsize];//a buffer needs to be created to hold the resulting decompressed data
        int ret2=-9;
        while (true){
            strm.next_out=decompBuffer;
            strm.avail_out=buffsize;
            int ret=inflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
            if( ret==Z_STREAM_END){//if we have decompressed the entire stream correctly, it must be valid
                if (strm.total_in>=16) ret2=0; else ret2=1;//dont care about streams shorter than 16 bytes
                break;//leave the function with 0 or 1
            }
            if (ret==Z_DATA_ERROR){//the stream is invalid
                ret2=1;
                break;//leave the function with 1
            }
            if (ret!=Z_BUF_ERROR){//if there is an error other than running out of output buffer
                std::cout<<"error, zlib returned with unexpected value: "<<ret<<std::endl;
                abort();
            }
            if (strm.avail_in==0){//if we get buf_error and ran out of input, get next chunk
                ret2=2;
                break;
            }
        }
        delete [] decompBuffer;
        return ret2;
    }
    void findDeflateParams_ALL(){
        //this function takes a filename and a vector containing information about the valid zlib streams in the file
        //it tries to find the best parameters for recompression, the results are stored in the vector
        uint64_t i;
        uint64_t numOffsets=streamOffsetList.size();
        for (i=0; i<numOffsets; i++){
            if ((options.concentrate>=0)&&(i==0)) {
                i=options.concentrate;
                numOffsets=options.concentrate;
            }
            unsigned char* rBuffer=new unsigned char[streamOffsetList[i].streamLength];
            std::ifstream in;
            in.open(infileName, std::ios::in | std::ios::binary);//open the input file
            in.seekg(streamOffsetList[i].offset);//seek to the beginning of the stream
            in.read(reinterpret_cast<char*>(rBuffer), streamOffsetList[i].streamLength);
            in.close();
            //a buffer needs to be created to hold the resulting decompressed data
            //since we have already deompressed the data before, we know exactly how large of a buffer we need to allocate
            //the lengths of the zlib streams have been saved by the previous phase
            unsigned char* decompBuffer= new unsigned char[streamOffsetList[i].inflatedLength];
            int ret=doInflate(rBuffer, streamOffsetList[i].streamLength, decompBuffer, streamOffsetList[i].inflatedLength);
            //check the return value
            if (ret==Z_STREAM_END){
                #ifdef debug
                std::cout<<std::endl;
                std::cout<<"stream #"<<i<<"("<<streamOffsetList[i].offset<<")"<<" ready for recompression trials"<<std::endl;
                #endif // debug
                findDeflateParams_stream(rBuffer, decompBuffer, streamOffsetList[i]);
            } else {//shit hit the fan, should never happen
                std::cout<<"inflate() failed with exit code:"<<ret<<std::endl;
                abort();
            }
            if (((streamOffsetList[i].streamLength-streamOffsetList[i].identBytes)<=options.recompTresh)&&(streamOffsetList[i].identBytes>0)){
                streamOffsetList[i].recomp=true;
            }
            delete [] decompBuffer;
            delete [] rBuffer;
        }
    }
    int doInflate(unsigned char* next_in, uint64_t avail_in, unsigned char* next_out, uint64_t avail_out){
        //this function takes a zlib stream from next_in and decompresses it to next_out, returning the return value of inflate()
        //the zlib stream must be at most avail_in bytes long and the inflated data must be at most avail_out bytes long
        //this is a nice, self-contained function, but it does everthing in one pass, so best suited for small streams
        z_stream strm;
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.avail_in = avail_in;
        strm.next_in=next_in;
        int ret=inflateInit(&strm);//initialize the stream for decompression and check for error
        if (ret != Z_OK){
            std::cout<<"inflateInit() failed with exit code:"<<ret<<std::endl;//should never happen normally
            abort();
        }
        strm.next_out=next_out;
        strm.avail_out=avail_out;
        int ret2=inflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
        //deallocate the zlib stream, check for errors and deallocate the decompression buffer
        ret=inflateEnd(&strm);
        if (ret!=Z_OK){
            std::cout<<"inflateEnd() failed with exit code:"<<ret<<std::endl;//should never happen normally
            abort();
        }
        return ret2;
    }
    void findDeflateParams_stream(unsigned char rBuffer[], unsigned char decompBuffer[], ATZdata::streamOffset& streamobj){
        uint8_t window= 10 + (streamobj.offsetType / 4);
        uint8_t crange = streamobj.offsetType % 4;
        #ifdef debug
        std::cout<<"   stream type: "<<streamobj.offsetType<<std::endl;
        std::cout<<"   window and crange from header: "<<+window<<" ; "<<+crange<<std::endl;
        #endif // debug
        //try the most probable parameters first(supplied by header or default)
        switch (crange){//we need to switch based on the clevel
            case 0:{//if the header signals fastest compression try clevel 1 and 0, header-supplied window and default memlvl(8)
                #ifdef debug
                std::cout<<"   trying most probable parameters: fastest compression"<<std::endl;
                #endif // debug
                if (testDeflateParams(rBuffer, decompBuffer, streamobj, 0, window, 8)) break;
                if (testDeflateParams(rBuffer, decompBuffer, streamobj, 1, window, 8)) break;
                //if the most probable parameters are not succesful, try all different clevel and memlevel combinations
                #ifdef debug
                std::cout<<"   trying less probable parameters: fastest compression"<<std::endl;
                #endif // debug
                if (testDeflateParams(rBuffer, decompBuffer, streamobj, 1, window, 9)) break;//try all memlvls for the most probable clvl
                if (testParamRange(rBuffer, decompBuffer, streamobj, 1, 1, window, window, 1, 7)) break;
                //try all clvl/memlvl combinations that have not been tried yet
                testParamRange(rBuffer, decompBuffer, streamobj, 2, 9, window, window, 1, 9);
                break;
             }
            case 1:{//if the header signals fast compression try clevel 2-5, header-supplied window and default memlvl(8)
                #ifdef debug
                std::cout<<"   trying most probable parameters: fast compression"<<std::endl;
                #endif // debug
                if (testParamRange(rBuffer, decompBuffer, streamobj, 2, 5, window, window, 8, 8)) break;
                //if the most probable parameters are not succesful, try all different clevel and memlevel combinations
                #ifdef debug
                std::cout<<"   trying less probable parameters: fast compression"<<std::endl<<std::endl;
                #endif // debug
                if (testParamRange(rBuffer, decompBuffer, streamobj, 2, 5, window, window, 1, 7)) break;
                if (testParamRange(rBuffer, decompBuffer, streamobj, 2, 5, window, window, 9, 9)) break;
                if (testParamRange(rBuffer, decompBuffer, streamobj, 1, 1, window, window, 1, 9)) break;
                testParamRange(rBuffer, decompBuffer, streamobj, 6, 9, window, window, 1, 9);
                break;
            }
            case 2:{//if the header signals default compression only try clevel 6, header-supplied window and default memlvl(8)
                #ifdef debug
                std::cout<<"   trying most probable parameters: default compression"<<std::endl;
                #endif // debug
                if (testDeflateParams(rBuffer, decompBuffer, streamobj, 6, window, 8)) break;
                //if the most probable parameters are not succesful, try all different clevel and memlevel combinations
                #ifdef debug
                std::cout<<"   trying less probable parameters: default compression"<<std::endl<<std::endl;
                #endif // debug
                if (testDeflateParams(rBuffer, decompBuffer, streamobj, 6, window, 9)) break;
                if (testParamRange(rBuffer, decompBuffer, streamobj, 6, 6, window, window, 1, 7)) break;
                if (testParamRange(rBuffer, decompBuffer, streamobj, 1, 5, window, window, 1, 9)) break;
                testParamRange(rBuffer, decompBuffer, streamobj, 7, 9, window, window, 1, 9);
                break;
            }
            case 3:{//if the header signals best compression only try clevel 7-9, header-supplied window and default memlvl(8)
                #ifdef debug
                std::cout<<"   trying most probable parameters: best compression"<<std::endl;
                #endif // debug
                if (testParamRange(rBuffer, decompBuffer, streamobj, 7, 9, window, window, 8, 8)) break;
                //if the most probable parameters are not succesful, try all different clevel and memlevel combinations
                #ifdef debug
                std::cout<<"   trying less probable parameters: best compression"<<std::endl<<std::endl;
                #endif // debug
                if (testParamRange(rBuffer, decompBuffer, streamobj, 7, 9, window, window, 1, 7)) break;
                if (testParamRange(rBuffer, decompBuffer, streamobj, 7, 9, window, window, 9, 9)) break;
                testParamRange(rBuffer, decompBuffer, streamobj, 1, 6, window, window, 1, 9);
                break;
            }
            default:{//this should never happen
                abort();
            }
        }
        if (((streamobj.streamLength-streamobj.identBytes)>=options.mismatchTol)&&(options.bruteforceWindow)){//if bruteforcing is turned on and needed, try all remaining combinations
            if (window==10){
                testParamRange(rBuffer, decompBuffer, streamobj, 1, 9, 11, 15, 1, 9);
            }else{
                if (window==15){
                    testParamRange(rBuffer, decompBuffer, streamobj, 1, 9, 10, 14, 1, 9);
                }else{//if window is in the 11-14 range
                    if (testParamRange(rBuffer, decompBuffer, streamobj, 1, 9, 10, (window-1), 1, 9)) return;
                    testParamRange(rBuffer, decompBuffer, streamobj, 1, 9, (window+1), 15, 1, 9);
                }
            }
        }
    }
    bool testDeflateParams(unsigned char origstream[], unsigned char decompbuff[], ATZdata::streamOffset& streamobj,
                        const uint8_t clevel, const uint8_t window, const uint8_t memlevel){
        //tests if the supplied deflate params(clevel, memlevel, window) are better for recompressing the given streamoffset
        //if yes, then update the streamoffset object to the new best values, and if mismatch is within tolerance then return true
        int ret;
        uint64_t i;
        bool fullmatch=false;
        uint64_t identBytes;
        #ifdef debug
        std::cout<<"-------------------------"<<std::endl;
        std::cout<<"   memlevel:"<<+memlevel<<std::endl;
        std::cout<<"   clevel:"<<+clevel<<std::endl;
        std::cout<<"   window:"<<+window<<std::endl;
        #endif // debug
        z_stream strm;//prepare the z_stream
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.next_in= decompbuff;
        ret = deflateInit2(&strm, clevel, Z_DEFLATED, window, memlevel, Z_DEFAULT_STRATEGY);
        if (ret != Z_OK){//initialize it and check for error
            std::cout<<"deflateInit() failed with exit code:"<<ret<<std::endl;//should never happen normally
            abort();
        }
        //create a buffer to hold the recompressed data
        unsigned char* recompBuffer= new unsigned char[deflateBound(&strm, streamobj.inflatedLength)];
        strm.avail_in= streamobj.inflatedLength;
        strm.next_out= recompBuffer;
        bool doFullStream=true;
        bool shortcut=false;
        if ((options.shortcutEnabled)&&(streamobj.streamLength>options.shortcutLength)){//if the stream is big and shortcuts are enabled
            shortcut=true;
            identBytes=0;
            strm.avail_out=options.shortcutLength;//only get a portion of the compressed data
            ret=deflate(&strm, Z_FINISH);
            if ((ret!=Z_STREAM_END)&&(ret!=Z_OK)){//most of the times the compressed data wont fit and we get Z_OK
                std::cout<<"deflate() in shorcut failed with exit code:"<<ret<<std::endl;//should never happen normally
                abort();
            }
            #ifdef debug
            std::cout<<"   shortcut: "<<strm.total_in<<" bytes compressed to "<<strm.total_out<<" bytes"<<std::endl;
            #endif // debug
            for (i=0;i<strm.total_out;i++){
                if (recompBuffer[i]==origstream[i]){
                    identBytes++;
                }
            }
            if (identBytes<(uint64_t)(options.shortcutLength-options.recompTresh)) doFullStream=false;//if we have too many mismatches bail early
            #ifdef debug
            std::cout<<"   shortcut: "<<identBytes<<" bytes out of "<<strm.total_out<<" identical"<<std::endl;
            #endif // debug
        }
        if (doFullStream){
            identBytes=0;
            if (shortcut){
                strm.avail_out=deflateBound(&strm, streamobj.inflatedLength)-options.shortcutLength;
            }else{
                strm.avail_out=deflateBound(&strm, streamobj.inflatedLength);
            }
            ret=deflate(&strm, Z_FINISH);//do the actual compression
            //check the return value to see if everything went well
            if (ret != Z_STREAM_END){
                std::cout<<"deflate() failed with exit code:"<<ret<<std::endl;
                abort();
            }
            #ifdef debug
            std::cout<<"   size difference: "<<(static_cast<int64_t>(strm.total_out)-static_cast<int64_t>(streamobj.streamLength))<<std::endl;
            #endif // debug
            uint64_t smaller;
            if (abs((strm.total_out-streamobj.streamLength))<=options.sizediffTresh){//if the size difference is not more than the treshold
                if (strm.total_out<streamobj.streamLength){//this is to prevent an array overread
                    smaller=strm.total_out;
                } else {
                    smaller=streamobj.streamLength;
                }
                for (i=0; i<smaller;i++){
                    if (recompBuffer[i]==origstream[i]){
                        identBytes++;
                    }
                }
                #ifdef debug
                std::cout<<"   diffBytes: "<<(streamobj.streamLength-identBytes)<<std::endl;
                #endif // debug
                if (identBytes>streamobj.identBytes){//if this recompressed stream has more matching bytes than the previous best
                    streamobj.identBytes=identBytes;
                    streamobj.clevel=clevel;
                    streamobj.memlvl=memlevel;
                    streamobj.window=window;
                    streamobj.firstDiffByte=-1;
                    streamobj.diffByteOffsets.clear();
                    streamobj.diffByteVal.clear();
                    if (identBytes==streamobj.streamLength){//if we have a full match set the flag to bail from the nested loops
                        #ifdef debug
                        std::cout<<"   recompression succesful, full match"<<std::endl;
                        #endif // debug
                        fullmatch=true;
                    }else{//there are different bytes and/or bytes at the end
                        std::vector<uint64_t> rawdiff;
                        if (identBytes+options.mismatchTol>=streamobj.streamLength) fullmatch=true;//if at most mismatchTol bytes diff bail from the loop
                        for (i=0; i<smaller;i++){//diff it
                            if (recompBuffer[i]!=origstream[i]){//if a mismatching byte is found
                                rawdiff.push_back(i);
                                streamobj.diffByteVal.push_back(origstream[i]);
                            }
                        }
                        if (strm.total_out<streamobj.streamLength){//if the recompressed stream is shorter we need to add bytes after diffing
                            for (i=strm.total_out; i<streamobj.streamLength; i++){//adding bytes
                                rawdiff.push_back(i);
                                streamobj.diffByteVal.push_back(origstream[i]);
                            }
                        }
                        deltaEncode(rawdiff, streamobj);
                    }
                }
            }
            #ifdef debug
            else{
                std::cout<<"   size difference is greater than "<<options.sizediffTresh<<" bytes, not comparing"<<std::endl;
            }
            #endif // debug
        }
        ret=deflateEnd(&strm);
        if ((ret != Z_OK)&&!((ret==Z_DATA_ERROR) && (!doFullStream))){//Z_DATA_ERROR is only acceptable if we skipped the full recompression
            std::cout<<"deflateEnd() failed with exit code:"<<ret<<std::endl;//should never happen normally
            abort();
        }
        delete [] recompBuffer;
        return fullmatch;
    }
    inline bool testParamRange(unsigned char origbuff[], unsigned char decompbuff[], ATZdata::streamOffset& streamobj, const uint8_t clevel_min,
                                const uint8_t clevel_max, const uint8_t window_min, const uint8_t window_max,
                                const uint8_t memlevel_min, const uint8_t memlevel_max){
        //this function tests a given range of deflate parameters
        uint8_t clevel, memlevel, window;
        bool fullmatch;
        for(window=window_max; window>=window_min; window--){
            for(memlevel=memlevel_max; memlevel>=memlevel_min; memlevel--){
                for(clevel=clevel_max; clevel>=clevel_min; clevel--){
                    fullmatch=testDeflateParams(origbuff, decompbuff, streamobj, clevel, window, memlevel);
                    if (fullmatch){
                        #ifdef debug
                        std::cout<<"   recompression succesful within tolerance, bailing"<<std::endl;
                        #endif // debug
                        return true;
                    }
                }
            }
        }
        return false;
    }
    inline void deltaEncode(const std::vector<uint64_t>& invec, ATZdata::streamOffset& streamobj){
        streamobj.firstDiffByte=invec[0];
        streamobj.diffByteOffsets.push_back(0);
        for (uint64_t i=1; i<invec.size(); i++){
            streamobj.diffByteOffsets.push_back(invec[i]-invec[i-1]);
        }
    }
    void writeATZfile(std::string ifname, std::string ofname, uint64_t chunksize){
        std::ofstream outfile;
        uint64_t atzlen,i;
        uint64_t lastos=0;
        uint64_t lastlen=0;
        if (ATZutil::getFilesize(ifname, infileSize)!=0) abort();
        outfile.open(ofname, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!outfile.is_open()){
            std::cout << "error: open file for output failed!" << std::endl;
            abort();
        }
        outfile.write("ATZ\1", 4); // File header and version
        writeNumber8(outfile, 0); // Placeholder for the length of the atz file
        writeNumber8(outfile, infileSize); // Length of the original file
        writeNumber8(outfile, countRecomp()); // Number of recompressed streams
        for(i=0;i<streamOffsetList.size();i++){//write recompressed stream descriptions
            if (streamOffsetList[i].recomp==true){//we are operating on the j-th stream
                writeStreamdesc(outfile, ifname, streamOffsetList[i]);
            }
        }
        for(i=0;i<streamOffsetList.size();i++){//write the gaps before streams and non-recompressed streams to disk as the residue
            if ((lastos+lastlen)!=streamOffsetList[i].offset){//there is a gap before the stream, copy the gap
                ATZutil::copyto(outfile, ifname, (streamOffsetList[i].offset-(lastos+lastlen)), (lastos+lastlen), chunksize);
            }
            if (streamOffsetList[i].recomp==false){//if the stream is not recompressed copy it
                ATZutil::copyto(outfile, ifname, streamOffsetList[i].streamLength, streamOffsetList[i].offset, chunksize);
            }
            lastos=streamOffsetList[i].offset;
            lastlen=streamOffsetList[i].streamLength;
        }
        if((lastos+lastlen)<infileSize){//if there is stuff after the last stream, write that to disk too
            ATZutil::copyto(outfile, ifname, (infileSize-(lastos+lastlen)), (lastos+lastlen), chunksize);
        }
        atzlen=outfile.tellp();
        std::cout<<"Total bytes written: "<<atzlen<<std::endl;
        outfile.seekp(4);//go back to the placeholder
        writeNumber8(outfile, atzlen);
    }
    inline void writeNumber8(std::ofstream &outfile, uint64_t number){
        outfile.write(reinterpret_cast<char*>(&number), 8);
    }
    void writeStreamdesc(std::ofstream& outfile, std::string ifname, ATZdata::streamOffset& streamobj){
        uint64_t i;
        writeNumber8(outfile, streamobj.offset);
        writeNumber8(outfile, streamobj.streamLength);
        writeNumber8(outfile, streamobj.inflatedLength);
        writeNumber1(outfile, streamobj.clevel);
        writeNumber1(outfile, streamobj.window);
        writeNumber1(outfile, streamobj.memlvl);
        uint64_t diffbytes=streamobj.diffByteOffsets.size();
        writeNumber8(outfile, diffbytes);
        if (diffbytes>0){
            writeNumber8(outfile, streamobj.firstDiffByte);
            for(i=0;i<diffbytes;i++){
                writeNumber8(outfile, streamobj.diffByteOffsets[i]);
            }
            for(i=0;i<diffbytes;i++){
                writeNumber1(outfile, streamobj.diffByteVal[i]);
            }
        }
        unsigned char* decompBuffer= new unsigned char[streamobj.inflatedLength];
        unsigned char* readBuffer= new unsigned char[streamobj.streamLength];
        ATZutil::read2buff(ifname, readBuffer, streamobj.streamLength, streamobj.offset);
        doInflate(readBuffer, streamobj.streamLength, decompBuffer, streamobj.inflatedLength);
        outfile.write(reinterpret_cast<char*>(decompBuffer), streamobj.inflatedLength);
        delete [] decompBuffer;
        delete [] readBuffer;
    }
    inline void writeNumber1(std::ofstream &outfile, uint8_t number) {
        outfile.write(reinterpret_cast<char*>(&number), 1);
    }
    void printStreaminfo_ALL(uint_fast16_t mismatchTol){
        uint64_t i,j, numFullmatch=0;
        std::cout<<"Stream info"<<std::endl;
        for (j=0; j<streamOffsetList.size(); j++){
            std::cout<<"-------------------------"<<std::endl;
            std::cout<<"   stream #"<<j<<std::endl;
            std::cout<<"   offset:"<<streamOffsetList[j].offset<<std::endl;
            std::cout<<"   memlevel:"<<+streamOffsetList[j].memlvl<<std::endl;
            std::cout<<"   clevel:"<<+streamOffsetList[j].clevel<<std::endl;
            std::cout<<"   window:"<<+streamOffsetList[j].window<<std::endl;
            std::cout<<"   best match:"<<streamOffsetList[j].identBytes<<" out of "<<streamOffsetList[j].streamLength<<std::endl;
            std::cout<<"   diffBytes:"<<streamOffsetList[j].diffByteOffsets.size()<<std::endl;
            std::cout<<"   diffVals:"<<streamOffsetList[j].diffByteVal.size()<<std::endl;
            std::cout<<"   mismatched bytes:";
            for (i=0; i<streamOffsetList[j].diffByteOffsets.size(); i++){
                std::cout<<streamOffsetList[j].diffByteOffsets[i]<<";";
            }
            std::cout<<std::endl;
        }
        std::cout<<"-------------------------"<<std::endl;
        for (j=0; j<streamOffsetList.size(); j++){
            if (((streamOffsetList[j].streamLength-streamOffsetList[j].identBytes)<=mismatchTol)&&(streamOffsetList[j].identBytes>0)) numFullmatch++;
        }
        std::cout<<"fullmatch streams:"<<numFullmatch<<" out of "<<streamOffsetList.size()<<std::endl;
    }
};

class ATZreconstructor{
public:
    ATZreconstructor()=delete;
    ATZreconstructor(std::string atz, std::string rec){
        atzfileName=atz;
        reconfileName=rec;
    }
    int reconstructATZ(const uint64_t chunksize){
        uint64_t origlen=0;
        uint64_t nstrms=0;
        uint64_t atzfileSize=0;
        std::vector<ATZdata::streamOffset> streamOffsetList;

        std::cout<<"reconstructing from "<<atzfileName<<std::endl;
        if (parseATZheader(atzfileName, origlen, nstrms)!=0) return -1;
        ATZutil::getFilesize(atzfileName, atzfileSize);
        std::cout<<"ATZ file size: "<<atzfileSize<<std::endl;
        std::cout<<"Original file size: "<<origlen<<std::endl;
        if (nstrms>0){
            //reead in all the info about the streams
            streamOffsetList.reserve(nstrms);
            uint64_t residueos=readStreamdesc_ALL(atzfileName, streamOffsetList, nstrms);
            pauser_debug();
            //do the reconstructing
            uint64_t gapsum=0;
            uint64_t lastos=0;
            uint64_t lastlen=0;
            uint64_t j;
            std::ofstream recfile(reconfileName, std::ios::out | std::ios::binary | std::ios::trunc);
            //write the gap before the stream(if the is one), then do the compression using the parameters from the ATZ file
            //then modify the compressed data according to the ATZ file(if necessary)
            for(j=0;j<streamOffsetList.size();j++){
                if ((lastos+lastlen)==streamOffsetList[j].offset){//no gap before the stream
                    #ifdef debug
                    std::cout<<"no gap before stream #"<<j<<std::endl;
                    #endif // debug
                }else{
                    #ifdef debug
                    std::cout<<"gap of "<<(streamOffsetList[j].offset-(lastos+lastlen))<<" bytes before stream #"<<j<<std::endl;
                    #endif // debug
                    //copy the gap
                    ATZutil::copyto(recfile, atzfileName, streamOffsetList[j].offset-(lastos+lastlen), residueos+gapsum, chunksize);
                    gapsum=gapsum+(streamOffsetList[j].offset-(lastos+lastlen));
                }
                #ifdef debug
                std::cout<<"reconstructing stream #"<<j<<std::endl;
                #endif // debug
                //a buffer needs to be created to hold the decompressed and the compressed data
                unsigned char* compBuffer= new unsigned char[streamOffsetList[j].streamLength+65535];
                unsigned char* readBuff= new unsigned char[streamOffsetList[j].inflatedLength];
                //read in the decompressed stream from ATZ file
                ATZutil::read2buff(atzfileName, readBuff, streamOffsetList[j].inflatedLength, streamOffsetList[j].atzInfos);
                doDeflate(readBuff, streamOffsetList[j].inflatedLength, compBuffer, streamOffsetList[j].streamLength+65535, streamOffsetList[j].clevel, streamOffsetList[j].window, streamOffsetList[j].memlvl);
                //do stream modification if needed
                if (streamOffsetList[j].firstDiffByte>=0){
                    uint64_t i, sum=0;
                    #ifdef debug
                    std::cout<<"   modifying "<<streamOffsetList[j].diffByteOffsets.size()<<" bytes"<<std::endl;
                    #endif // debug
                    uint64_t db=streamOffsetList[j].diffByteOffsets.size();
                    for(i=0;i<db;i++){
                        compBuffer[streamOffsetList[j].firstDiffByte+streamOffsetList[j].diffByteOffsets[i]+sum]=streamOffsetList[j].diffByteVal[i];
                        sum=sum+streamOffsetList[j].diffByteOffsets[i];
                    }
                }
                recfile.write(reinterpret_cast<char*>(compBuffer), streamOffsetList[j].streamLength);
                delete [] compBuffer;
                delete [] readBuff;
                lastos=streamOffsetList[j].offset;
                lastlen=streamOffsetList[j].streamLength;
            }
            if ((lastos+lastlen)<origlen){
                #ifdef debug
                std::cout<<"copying "<<(origlen-(lastos+lastlen))<<" bytes to the end of the file"<<std::endl;
                #endif // debug
                //copy the end of the original file after the last stream to finish reconstruction
                ATZutil::copyto(recfile, atzfileName, origlen-(lastos+lastlen), residueos+gapsum, chunksize);
            }
            recfile.close();
        }else{//if there are no recompressed streams
            #ifdef debug
            std::cout<<"no recompressed streams in the ATZ file, copying "<<origlen<<" bytes"<<std::endl;
            #endif // debug
            std::ofstream recfile(reconfileName, std::ios::out | std::ios::binary | std::ios::trunc);
            ATZutil::copyto(recfile, atzfileName, origlen, 28, chunksize);
            recfile.close();
        }
        return 0;
    }
private:
    std::string atzfileName;
    std::string reconfileName;
    inline uint64_t readNumber8(std::ifstream& infile){
        unsigned char buff[8];
        infile.read(reinterpret_cast<char*>(buff), 8);
        uint64_t output;
        memcpy(&output, buff, 8);
        return output;
    }
    inline uint64_t readNumber8(std::ifstream& infile, uint64_t pos){
        unsigned char buff[8];
        infile.seekg(pos);
        infile.read(reinterpret_cast<char*>(buff), 8);
        uint64_t output;
        memcpy(&output, buff, 8);
        return output;
    }
    inline uint8_t readNumber1(std::ifstream& infile, uint64_t pos){
        uint8_t output;
        infile.seekg(pos);
        infile.read(reinterpret_cast<char*>(&output), 1);
        return output;
    }
    void doDeflate(unsigned char* next_in, uint64_t avail_in, unsigned char* next_out, uint64_t avail_out, uint8_t clvl, uint8_t window, uint8_t memlvl){
        z_stream strm;
        int ret;
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.next_in=next_in;
        strm.avail_in=avail_in;
            //initialize the stream for compression and check for error
        ret=deflateInit2(&strm, clvl, Z_DEFLATED, window, memlvl, Z_DEFAULT_STRATEGY);
        if(ret!=Z_OK){
            std::cout<<"deflateInit() failed with exit code:"<<ret<<std::endl;
            abort();
        }
        strm.next_out=next_out;
        strm.avail_out=avail_out;
        ret=deflate(&strm, Z_FINISH);//try to do the actual decompression in one pass
        //check the return value
        if (ret!=Z_STREAM_END){
            std::cout<<"deflate() failed with exit code:"<<ret<<std::endl;
            abort();
        }
        ret=deflateEnd(&strm);
        if (ret!=Z_OK){
            std::cout<<"deflateEnd() failed with exit code:"<<ret<<std::endl;//should never happen normally
            abort();
        }
    }
    inline void pauser_debug(){
        #ifdef debug
            std::string dummy;
            std::cout << "Press enter to continue...";
            std::getline(std::cin, dummy);
        #endif // debug
    }
    int parseATZheader(std::string atzfile_name, uint64_t& origlen, uint64_t& nstrms){
        uint64_t infileSize=0;
        if (ATZutil::getFilesize(atzfile_name, infileSize)!=0) return -1;
        std::ifstream atzfile(atzfile_name, std::ios::in | std::ios::binary);
        //setting up read buffer and reading the entire file into the buffer
        unsigned char atzbuf[4];
        atzfile.read(reinterpret_cast<char*>(atzbuf), 4);
        if (atzbuf[0] != 'A' || atzbuf[1] != 'T' || atzbuf[2] != 'Z' || atzbuf[3] != '\1') {
            std::cout<<"Invalid file: ATZ1 header not found"<<std::endl;
            return -2;
        }
        if ((readNumber8(atzfile))!=infileSize){
            std::cout<<"Invalid file: ATZ file size mismatch"<<std::endl;
            return -3;
        }
        origlen=readNumber8(atzfile);
        nstrms=readNumber8(atzfile);
        atzfile.close();
        return 0;
    }
    uint64_t readStreamdesc_ALL(std::string atzfile_name, std::vector<ATZdata::streamOffset>& streamOffsetList, uint64_t nstrms){
        uint64_t i,j;
        uint64_t lastos=28;//start after the header part
        std::ifstream atzfile(atzfile_name, std::ios::in | std::ios::binary);
        for (j=0;j<nstrms;j++){
            #ifdef debug
            std::cout<<"stream #"<<j<<std::endl;
            #endif // debug
            streamOffsetList.push_back(ATZdata::streamOffset(readNumber8(atzfile, lastos), -1, readNumber8(atzfile, lastos+8), readNumber8(atzfile, lastos+16)));
            streamOffsetList[j].clevel=readNumber1(atzfile, lastos+24);
            streamOffsetList[j].window=readNumber1(atzfile, lastos+25);
            streamOffsetList[j].memlvl=readNumber1(atzfile, lastos+26);
            //partial match handling
            uint64_t diffbytes=readNumber8(atzfile, lastos+27);
            if (diffbytes>0){//if the stream is just a partial match
                streamOffsetList[j].firstDiffByte=readNumber8(atzfile, lastos+35);
                streamOffsetList[j].diffByteOffsets.reserve(diffbytes);
                streamOffsetList[j].diffByteVal.reserve(diffbytes);
                for (i=0;i<diffbytes;i++){
                    streamOffsetList[j].diffByteOffsets.push_back(readNumber8(atzfile, 43+8*i+lastos));
                    streamOffsetList[j].diffByteVal.push_back(readNumber1(atzfile, 43+diffbytes*8+i+lastos));
                }
                streamOffsetList[j].atzInfos=43+diffbytes*9+lastos;
                lastos=lastos+43+diffbytes*9+streamOffsetList[j].inflatedLength;
            }else{//if the stream is a full match
                streamOffsetList[j].firstDiffByte=-1;//negative value signals full match
                streamOffsetList[j].atzInfos=35+lastos;
                lastos=lastos+35+streamOffsetList[j].inflatedLength;
            }
        }
        atzfile.close();
        return lastos;
    }
};

void parseCLI(int, char* [], std::string&, std::string&, std::string&, ATZdata::programOptions&);
bool test_f2f(std::string, std::string, uint64_t);
int testATZfile(std::string infileName, std::string atzfileName, std::string reconfileName, uint64_t chunksize);

void parseCLI(int argc, char* argv[], std::string& infile_name, std::string& atzfile_name, std::string& reconfile_name, ATZdata::programOptions& options){
    // Wrap everything in a try block.  Do this every time,
	// because exceptions will be thrown for problems.
	try{
        // Define the command line object.
        TCLAP::CmdLine cmd("Visit https://github.com/Diazonium/AntiZ for source code and support.", ' ', antiz_ver);

        // Define a value argument and add it to the command line. This defines the input file.
        TCLAP::ValueArg<std::string> infileArg("i", "input", "Input file name", true, "", "string");
        cmd.add(infileArg);

        // Define the output file. This is optional, if not provided then it will be generated from the input file name.
        TCLAP::ValueArg<std::string> outfileArg("o", "output", "Output file name", false, "", "string");
        cmd.add(outfileArg);

        TCLAP::ValueArg<uint_fast16_t> recomptreshArg("", "recomp-tresh", "Recompression treshold in bytes. Streams are only recompressed if the best match differs from the original in at most recompTresh bytes. Increasing this treshold may allow more streams to be recompressed, but may increase ATZ file overhead and make it harder to compress. Default: 128  Maximum: 65535", false, 128, "integer");
        cmd.add(recomptreshArg);
        TCLAP::ValueArg<uint_fast16_t> sizedifftreshArg("", "sizediff-tresh", "Size difference treshold in bytes. If the size difference between a recompressed stream and the original is more than the treshold then do not even compare them. Increasing this treshold increases the chance that a stream will be compared to the original. The cost of comparing is relatively low, so setting this equal to the recompression treshold should be fine. Default: 128  Maximum: 65535", false, 128, "integer");
        cmd.add(sizedifftreshArg);
        TCLAP::ValueArg<uint_fast16_t> shortcutlenArg("", "shortcut-len", "Length of the shortcut in bytes. If a stream is longer than the shortcut, then stop compression after <shortcut> compressed bytes have been obtained and compare this portion to the original. If this comparison yields more than recompTresh mismatches, then do not compress the entire stream. Lowering this improves speed, but it must be significantly greater than recompTresh or the speed benefit will decrease. Default: 512  Maximum: 65535", false, 512, "integer");
        cmd.add(shortcutlenArg);
        TCLAP::ValueArg<uint_fast16_t> mismatchtolArg("", "mismatch-tol", "Mismatch tolerance in bytes. If a set of parameters are found that give at most this many mismatches, then accept them and stop looking for a better set of parameters. Increasing this improves speed at the cost of more ATZ file overhead that may hurt compression. Default: 2  Maximum: 65535", false, 2, "integer");
        cmd.add(mismatchtolArg);
        TCLAP::ValueArg<uint64_t> chunksizeArg("", "chunksize", "Size of the memory buffer in bytes for chunked disk IO. This contorls memory usage to some extent, but memory usage control is not fully implemented yet. Smaller values result in more disk IO operations. Default: 524288", false, 524288, "integer");
        cmd.add(chunksizeArg);//HAVE_LONG_LONG must be defined, or TCLAP will not handle 64bit ints

        // Define a switch and add it to the command line.
        TCLAP::SwitchArg reconSwitch("r", "reconstruct", "Assume the input file is an ATZ file and attempt to reconstruct the original file from it", false);
        cmd.add(reconSwitch);
        TCLAP::SwitchArg notestSwitch("", "notest", "Skip comparing the reconstructed file to the original at the end. This is not recommended, as AntiZ is still experimental software and my contain bugs that corrupt data.", false);
        cmd.add(notestSwitch);
        TCLAP::SwitchArg brutewindowSwitch("", "brute-window", "Bruteforce deflate window size if there is a chance that recompression could be improved by it. This can have a major performance penalty. Default: disabled", false);
        cmd.add(brutewindowSwitch);

        // Parse the args.
        cmd.parse( argc, argv );

        std::cout<<"Input file: "<<infileArg.getValue()<<std::endl;
        //use the parameters we get
        options.recompTresh= recomptreshArg.getValue();
        options.sizediffTresh= sizedifftreshArg.getValue();
        options.shortcutLength= shortcutlenArg.getValue();
        options.mismatchTol= mismatchtolArg.getValue();
        options.bruteforceWindow= brutewindowSwitch.getValue();
        options.chunksize= chunksizeArg.getValue();

        options.recon = reconSwitch.getValue();//check if we need to reconstruct only
        options.notest= notestSwitch.getValue();
        if (options.recon){
            std::cout<<"assuming input file is an ATZ file, attempting to reconstruct"<<std::endl;
            atzfile_name= infileArg.getValue();
            if (outfileArg.isSet()){//if the output is specified use that
                reconfile_name= outfileArg.getValue();
            }else{//if not, append .rec to the input file name
                reconfile_name= atzfile_name;
                reconfile_name.append(".rec");
            }
            std::cout<<"overwriting "<<reconfile_name<<" if present"<<std::endl;
        }else{
            infile_name= infileArg.getValue();
            if (outfileArg.isSet()){//if the output is specified use that
                atzfile_name= outfileArg.getValue();
            }else{//if not, append .atz to the input file name
                atzfile_name= infile_name;
                atzfile_name.append(".atz");
            }
            reconfile_name= infile_name;
            reconfile_name.append(".rec");
            std::cout<<"overwriting "<<atzfile_name<<" and "<<reconfile_name<<" if present"<<std::endl;
        }
	} catch (TCLAP::ArgException &e){  // catch any exceptions
        std::cout << "error: " << e.error() << " for arg " << e.argId() << std::endl;
    }
}

bool test_f2f(std::string fname1, std::string fname2, uint64_t chunksize){
    //returns true if the files are identical
    uint64_t fsize1, fsize2, i;
    fsize1=0;
    fsize2=0;
    ATZutil::getFilesize(fname1, fsize1);
    ATZutil::getFilesize(fname2, fsize2);
    if (fsize1!=fsize2) return false;
    ATZutil::inbuffer buffobj1(fname1, chunksize, 0);
    ATZutil::inbuffer buffobj2(fname2, chunksize, 0);
    if (fsize1<=chunksize){
        for (i=0;i<fsize1;i++){
            if (buffobj1.buff[i]!=buffobj2.buff[i]) return false;
        }
    }else{
        uint64_t done=0;
        while(done<fsize1){
            for (i=0;i<chunksize;i++){
                if (buffobj1.buff[i]!=buffobj2.buff[i]) return false;
                done++;
            }
            buffobj1.next_chunk();
            buffobj2.next_chunk();
        }
    }
    return true;
}

int testATZfile(std::string infileName, std::string atzfileName, std::string reconfileName, uint64_t chunksize){
    uint64_t infileSize=0;
    uint64_t recfileSize=0;
    ATZreconstructor reconATZ(atzfileName, reconfileName);
    if (reconATZ.reconstructATZ(chunksize)!=0) return -1;
    std::cout<<"Testing...";
    ATZutil::getFilesize(infileName, infileSize);
    ATZutil::getFilesize(reconfileName, recfileSize);
    #ifdef debug
    std::cout<<std::endl<<"Original file size:"<<infileSize<<std::endl;
    std::cout<<std::endl<<"Reconstructed file size:"<<recfileSize<<std::endl;
    #endif // debug
    if(infileSize!=recfileSize){
        std::cout<<"error: size mismatch";
        return -1;
    }
    if (!test_f2f(infileName, reconfileName, chunksize)){
        std::cout<<"error: byte mismatch";
        return -2;
    }
    std::cout<<"OK!"<<std::endl;
    if (remove(reconfileName.c_str())!=0){//delete the reconstructed file since it was only needed for testing
        std::cout<<"error: cannot delete recfile";
        return -3;
    }
    return 0;
}

int main(int argc, char* argv[]){
    std::cout<<"AntiZ "<<antiz_ver<<std::endl;
	std::string infile_name;
	std::string atzfile_name;
	std::string reconfile_name;
	ATZdata::programOptions options;

    //parse CLI arguments
    parseCLI(argc, argv, infile_name, atzfile_name, reconfile_name, options);//parse CLI arguments and if needed jump to reconstruction
    ATZutil::pauser_debug();

    if (!options.recon){
        ATZcreator createATZ(infile_name, atzfile_name, reconfile_name, options);
        if (createATZ.Phase1()!=0) return -1;
        ATZutil::pauser_debug();
        if (createATZ.Phase2()!=0) return -1;
        if (createATZ.Phase3()!=0) return -1;
        if (createATZ.Phase4()!=0) return -1;
        if (!options.notest){
            if (testATZfile(infile_name, atzfile_name, reconfile_name, options.chunksize)!=0) return -1;
            ATZutil::pauser_debug();
        }
    }else{
        ATZreconstructor reconATZ(atzfile_name, reconfile_name);
        if (reconATZ.reconstructATZ(options.chunksize)!=0) return -1;
    }
	return 0;
}
