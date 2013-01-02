//////////////////////////////////////////////////////////////////////////////
//
// (c) Copyright 2012 Blackrock Microsystems
//
// $Workfile: main.c $
// $Archive: /n2h5/main.c $
// $Revision: 1 $
// $Date: 11/1/12 1:00p $
// $Author: Ehsan $
//
// $NoKeywords: $
//
//////////////////////////////////////////////////////////////////////////////
//
// PURPOSE:
//
// Convert nsx and nev files to hdf5 format
//
// Multiple data groups can be merged in one file
//  For example to combine different experiments.
// Main data group populates the root itself, 
//  the next roots at "/group00001" and so forth
// The field GroupCount in the root attributes contains total data groups count
// While first level group names are part of the spec (/channel, /comment, /group00001, ...)
//  the sub-group names should not be relied upon, instead all children should be iterated
//  and attributes consulted to find the information about each sub-group
// To merge channels of the same experiment, it should be added to the same group
//  for example the raw recording of /channel/channel00001 can go to /channel/channel00001_1
//
//////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "n2h5.h"
#include "NevNsx.h"

#ifdef WIN32                          // Windows needs the different spelling
#define ftello _ftelli64
#define fseeko _fseeki64
#endif

 // TODO: optimize these numbers
#define CHUNK_SIZE_CONTINUOUS   (1024)
#define CHUNK_SIZE_EVENT        (1024)

UINT16 g_spikeLength = 48;
hid_t g_tid_spike = -1;
hid_t g_tid_dig = -1;
hid_t g_tid_comment = -1;
hid_t g_tid_tracking[cbMAXTRACKOBJ];
bool  g_bFixedLengthTracking[cbMAXTRACKOBJ];
hid_t g_tid_synch = -1;

// Author & Date:   Ehsan Azar   Nov 17, 2012
// Purpose: Create and add root
// Inputs:
//  pFile - the source file
//  file  - the destination file
//  isHdr - NEV header
// Outputs:
//   Returns 0 on success, error code otherwise
int AddRoot(FILE * pFile, hid_t file, NevHdr & isHdr)
{
    herr_t ret;
    hid_t tid_root_attr = CreateRootAttrType(file);

    BmiRootAttr_t header;
    memset(&header, 0, sizeof(header));
    header.nMajorVersion = 1;
    header.szApplication = isHdr.szApplication;
    header.szComment = isHdr.szComment;
    header.nGroupCount = 1;
    {
        TIMSTM ts;
        memset(&ts, 0, sizeof(ts));
        SYSTEMTIME & st = isHdr.isAcqTime;
        sprintf((char *)&ts, "%04hd-%02hd-%02hd %02hd:%02hd:%02hd.%06d",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                st.wSecond, st.wMilliseconds * 1000); 
        ts.chNull = '\0';
        header.szDate = (char *)&ts;
    }
    // Add file header as attribute of the root group
    {
        hsize_t     dims[1] = {1};
        hid_t space = H5Screate_simple(1, dims, NULL);
        hid_t gid_root = H5Gopen(file, "/", H5P_DEFAULT);
        hid_t aid_root = H5Acreate(gid_root, "BmiRoot", tid_root_attr, space, H5P_DEFAULT, H5P_DEFAULT);
        ret = H5Awrite(aid_root, tid_root_attr, &header);
        ret = H5Aclose(aid_root);
        ret = H5Gclose(gid_root);
        ret = H5Sclose(space);
    }
    H5Tclose(tid_root_attr);
    return 0;
}

// Author & Date:   Ehsan Azar   Nov 17, 2012
// Purpose: Create and add root
// Inputs:
//  szSrcFile - source file name
//  pFile     - the source file
//  file      - the destination file
//  isHdr     - Nsx 2.1 header
// Outputs:
//   Returns 0 on success, error code otherwise
int AddRoot(const char * szSrcFile, FILE * pFile, hid_t file, Nsx21Hdr & isHdr)
{
    herr_t ret;
    hid_t tid_root_attr = CreateRootAttrType(file);

    BmiRootAttr_t header;
    memset(&header, 0, sizeof(header));
    header.nMajorVersion = 1;
    header.szApplication = isHdr.szGroup;
    header.szComment = "";
    header.nGroupCount = 1;
    TIMSTM ts;
    memset(&ts, 0, sizeof(ts));
#ifdef WIN32
    WIN32_FILE_ATTRIBUTE_DATA fattr;
    if (!GetFileAttributesEx(szSrcFile, GetFileExInfoStandard, &fattr))
    {
        printf("Cannot get file attributes\n");
        return 1;
    }
    SYSTEMTIME st;
    FileTimeToSystemTime(&fattr.ftCreationTime, &st);
    sprintf((char *)&ts, "%04hd-%02hd-%02hd %02hd:%02hd:%02hd.%06d",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
            st.wSecond, st.wMilliseconds * 1000); 
    ts.chNull = '\0';
    header.szDate = (char *)&ts;
#else
    struct stat st;
    if (stat(szSrcFile, &st))
    {
        printf("Cannot get file attributes\n");
        return 1;
    }
    time_t t = st.st_mtime;
    // TODO: use strftime to convert to st
#endif
    // Add file header as attribute of the root group
    {
        hsize_t     dims[1] = {1};
        hid_t space = H5Screate_simple(1, dims, NULL);
        hid_t gid_root = H5Gopen(file, "/", H5P_DEFAULT);
        hid_t aid_root = H5Acreate(gid_root, "BmiRoot", tid_root_attr, space, H5P_DEFAULT, H5P_DEFAULT);
        ret = H5Awrite(aid_root, tid_root_attr, &header);
        ret = H5Aclose(aid_root);
        ret = H5Gclose(gid_root);
        ret = H5Sclose(space);
    }
    H5Tclose(tid_root_attr);
    return 0;
}

// Author & Date:   Ehsan Azar   Nov 17, 2012
// Purpose: Create and add root
// Inputs:
//  pFile - the source file
//  file  - the destination file
//  isHdr - NSx 2.2 header
// Outputs:
//   Returns 0 on success, error code otherwise
int AddRoot(FILE * pFile, hid_t file, Nsx22Hdr & isHdr)
{
    herr_t ret;
    hid_t tid_root_attr = CreateRootAttrType(file);

    BmiRootAttr_t header;
    memset(&header, 0, sizeof(header));
    header.nMajorVersion = 1;
    header.szApplication = isHdr.szGroup;
    header.szComment = isHdr.szComment;
    header.nGroupCount = 1;
    {
        TIMSTM ts;
        memset(&ts, 0, sizeof(ts));
        SYSTEMTIME & st = isHdr.isAcqTime;
        sprintf((char *)&ts, "%04hd-%02hd-%02hd %02hd:%02hd:%02hd.%06d",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                st.wSecond, st.wMilliseconds * 1000); 
        ts.chNull = '\0';
        header.szDate = (char *)&ts;
    }
    // Add file header as attribute of the root group
    {
        hsize_t     dims[1] = {1};
        hid_t space = H5Screate_simple(1, dims, NULL);
        hid_t gid_root = H5Gopen(file, "/", H5P_DEFAULT);
        hid_t aid_root = H5Acreate(gid_root, "BmiRoot", tid_root_attr, space, H5P_DEFAULT, H5P_DEFAULT);
        ret = H5Awrite(aid_root, tid_root_attr, &header);
        ret = H5Aclose(aid_root);
        ret = H5Gclose(gid_root);
        ret = H5Sclose(space);
    }
    H5Tclose(tid_root_attr);
    return 0;
}

// Author & Date:   Ehsan Azar   Nov 17, 2012
// Purpose: Create groups and packet tables for sampled data
// Inputs:
//  pFile - the source file
//  file  - the destination file
// Outputs:
//  isHdr - NEV header
//  Returns 0 on success, error code otherwise
int CreateNevGroups(FILE * pFile, hid_t file, NevHdr & isHdr)
{
    herr_t ret;
    fseeko(pFile, 0, SEEK_SET);    // read header from beginning of file
    if (fread(&isHdr, sizeof(isHdr), 1, pFile) != 1)
    {
        printf("Cannot read source file header\n");
        return 1;
    }

    // Add root attribute
    if (AddRoot(pFile, file, isHdr))
        return 1;

    // 21, 22, 23 flat file verion number
    UINT32 nVer = isHdr.byFileRevMajor * 10 + isHdr.byFileRevMinor;

    g_spikeLength = (isHdr.dwBytesPerPacket - 8) / 2;

    char * szMapFile = NULL;
    BmiTrackingAttr_t trackingAttr[cbMAXTRACKOBJ];
    memset(trackingAttr, 0, sizeof(trackingAttr));
    BmiSynchAttr_t synchAttr;
    memset(&synchAttr, 0, sizeof(synchAttr));
    BmiDigChanAttr_t digChanAttr[cbNUM_DIGIN_CHANS + cbNUM_SERIAL_CHANS];
    memset(digChanAttr, 0, sizeof(digChanAttr));
    BmiChanAttr_t chanAttr[cbNUM_ANALOG_CHANS];
    memset(chanAttr, 0, sizeof(chanAttr));
    BmiChanExtAttr_t chanExtAttr[cbNUM_ANALOG_CHANS];
    memset(chanExtAttr, 0, sizeof(chanExtAttr));
    // NEV provides Ext1 additional header
    BmiChanExt1Attr_t chanExt1Attr[cbNUM_ANALOG_CHANS];
    memset(chanExt1Attr, 0, sizeof(chanExt1Attr));
    // Read the header to fill channel attributes
    for (UINT32 i = 0; i < isHdr.dwNumOfExtendedHeaders; ++i)
    {
        NevExtHdr isExtHdr;
        if (fread(&isExtHdr, sizeof(isExtHdr), 1, pFile) != 1)
        {
            printf("Invalid source file header\n");
            return 1;
        }
        if (0 == strncmp(isExtHdr.achPacketID, "NEUEVWAV", sizeof(isExtHdr.achPacketID)))
        {
            if (isExtHdr.neuwav.wave_samples != 0)
                g_spikeLength = isExtHdr.neuwav.wave_samples;
            int id = isExtHdr.id;
            if (id == 0 || id > cbNUM_ANALOG_CHANS)
            {
                printf("Invalid channel ID in source file header\n");
                return 1;
            }
            id--; // make it zero-based
            chanAttr[id].id = isExtHdr.id;
            // Currently all spikes are sampled at clock rate
            chanAttr[id].fClock = float(isHdr.dwTimeStampResolutionHz);
            chanAttr[id].fSampleRate = float(isHdr.dwSampleResolutionHz);
            chanAttr[id].nSampleBits = isExtHdr.neuwav.wave_bytes * 8;

            chanExtAttr[id].phys_connector = isExtHdr.neuwav.phys_connector;
            chanExtAttr[id].connector_pin = isExtHdr.neuwav.connector_pin;
            chanExtAttr[id].dFactor = isExtHdr.neuwav.digital_factor;

            chanExt1Attr[id].energy_thresh = isExtHdr.neuwav.energy_thresh;
            chanExt1Attr[id].high_thresh = isExtHdr.neuwav.high_thresh;
            chanExt1Attr[id].low_thresh = isExtHdr.neuwav.low_thresh;
            chanExt1Attr[id].sortCount = isExtHdr.neuwav.sorted_count;

        }
        else if (0 == strncmp(isExtHdr.achPacketID, "NEUEVLBL", sizeof(isExtHdr.achPacketID)))
        {
            int id = isExtHdr.id;
            if (id == 0 || id > cbNUM_ANALOG_CHANS)
            {
                printf("Invalid channel ID in source file header\n");
                return 1;
            }
            id--;
            chanExtAttr[id].szLabel = _strdup(isExtHdr.neulabel.label);
        }
        else if (0 == strncmp(isExtHdr.achPacketID, "NEUEVFLT", sizeof(isExtHdr.achPacketID)))
        {
            int id = isExtHdr.id;
            if (id == 0 || id > cbNUM_ANALOG_CHANS)
            {
                printf("Invalid channel ID in source file header\n");
                return 1;
            }
            id--;
            chanExtAttr[id].filter.hpfreq = isExtHdr.neuflt.hpfreq;
            chanExtAttr[id].filter.hporder = isExtHdr.neuflt.hporder;
            chanExtAttr[id].filter.hptype = isExtHdr.neuflt.hptype;
            chanExtAttr[id].filter.lpfreq = isExtHdr.neuflt.lpfreq;
            chanExtAttr[id].filter.lporder = isExtHdr.neuflt.lporder;
            chanExtAttr[id].filter.lptype = isExtHdr.neuflt.lptype;
        }
        else if (0 == strncmp(isExtHdr.achPacketID, "VIDEOSYN", sizeof(isExtHdr.achPacketID)))
        {
            synchAttr.id = isExtHdr.id;
            synchAttr.fFps = isExtHdr.videosyn.fFps;
            synchAttr.szLabel = _strdup(isExtHdr.videosyn.label);
        }
        else if (0 == strncmp(isExtHdr.achPacketID, "TRACKOBJ", sizeof(isExtHdr.achPacketID)))
        {
            int id = isExtHdr.trackobj.trackID; // 1-based
            if (id == 0 || id > cbMAXTRACKOBJ)
            {
                printf("Invalid trackable ID in source file header\n");
                return 1;
            }
            id--;
            trackingAttr[id].type = isExtHdr.id; // 0-based type
            trackingAttr[id].trackID = isExtHdr.trackobj.trackID;
            trackingAttr[id].maxPoints = isExtHdr.trackobj.maxPoints;
            trackingAttr[id].szLabel = _strdup(isExtHdr.trackobj.label);
        }
        else if (0 == strncmp(isExtHdr.achPacketID, "MAPFILE", sizeof(isExtHdr.achPacketID)))
        {
            szMapFile = _strdup(isExtHdr.mapfile.label);
        }
        else if (0 == strncmp(isExtHdr.achPacketID, "DIGLABEL", sizeof(isExtHdr.achPacketID)))
        {
            int id = 0;
            if (isExtHdr.diglabel.mode == 0)
            {
                id = 1; // Serial
                digChanAttr[id].id = cbFIRST_SERIAL_CHAN + 1;
            }
            else if (isExtHdr.diglabel.mode == 1)
            {
                id = 0; // Digital
                digChanAttr[id].id = cbFIRST_DIGIN_CHAN + 1;
            } else {
                printf("Invalid digital input mode in source file header\n");
                return 1;
            }
            digChanAttr[id].szLabel = _strdup(isExtHdr.diglabel.label);
        } else {
            printf("Unknown header (%7s) in the source file\n", isExtHdr.achPacketID);
        }
    } // end for (UINT32 i = 0

    hsize_t     dims[1] = {1};
    hid_t space_attr = H5Screate_simple(1, dims, NULL);

    // Add channel group
    {
        hid_t gid_channel = H5Gcreate(file, "channel", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        if (szMapFile != NULL)
        {
            hid_t tid_attr_vl_str = H5Tcopy(H5T_C_S1);
            ret = H5Tset_size(tid_attr_vl_str, H5T_VARIABLE);
            hid_t aid = H5Acreate(gid_channel, "MapFile", tid_attr_vl_str, space_attr, H5P_DEFAULT, H5P_DEFAULT);
            ret = H5Awrite(aid, tid_attr_vl_str, szMapFile);
            ret = H5Aclose(aid);
            H5Tclose(tid_attr_vl_str);
        }
        g_tid_spike = CreateSpike16Type(gid_channel, g_spikeLength);
        hid_t tid_chan_attr = CreateChanAttrType(gid_channel);
        hid_t tid_chanext_attr = CreateChanExtAttrType(gid_channel);
        hid_t tid_chanext1_attr = CreateChanExt1AttrType(gid_channel);
        for (int i = 0; i < cbNUM_ANALOG_CHANS; ++i)
        {
            if (chanAttr[i].id == 0)
                continue;
            char szNum[6];
            std::string strLabel = "channel";
            sprintf(szNum, "%05u", chanAttr[i].id);
            strLabel += szNum;
            hid_t gid = H5Gcreate(gid_channel, strLabel.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

            // Basic channel attributes
            hid_t aid = H5Acreate(gid, "BmiChan", tid_chan_attr, space_attr, H5P_DEFAULT, H5P_DEFAULT);
            ret = H5Awrite(aid, tid_chan_attr, &chanAttr[i]);
            ret = H5Aclose(aid);

            // Extra channel attributes
            aid = H5Acreate(gid, "BmiChanExt", tid_chanext_attr, space_attr, H5P_DEFAULT, H5P_DEFAULT);
            ret = H5Awrite(aid, tid_chanext_attr, &chanExtAttr[i]);
            ret = H5Aclose(aid);

            // Additional extra channel attributes
            aid = H5Acreate(gid, "BmiChanExt1", tid_chanext1_attr, space_attr, H5P_DEFAULT, H5P_DEFAULT);
            ret = H5Awrite(aid, tid_chanext1_attr, &chanExt1Attr[i]);
            ret = H5Aclose(aid);

            ret = H5Gclose(gid);
        }
        ret = H5Tclose(tid_chanext1_attr);
        ret = H5Tclose(tid_chanext_attr);
        ret = H5Tclose(tid_chan_attr);

        // Add digital and serial channel and their attributes
        {
            g_tid_dig = CreateDig16Type(gid_channel);
            hid_t gid = H5Gcreate(gid_channel, "digital1", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
            hid_t tid = CreateDigChanAttrType(gid_channel);
            if (digChanAttr[0].id)
            {
                hid_t aid = H5Acreate(gid, "BmiDigChan", tid, space_attr, H5P_DEFAULT, H5P_DEFAULT);
                ret = H5Awrite(aid, tid, &digChanAttr[0]);
                ret = H5Aclose(aid);
            }
            ret = H5Gclose(gid);

            gid = H5Gcreate(gid_channel, "serial1", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
            if (digChanAttr[1].id)
            {
                hid_t aid = H5Acreate(gid, "BmiDigChan", tid, space_attr, H5P_DEFAULT, H5P_DEFAULT);
                ret = H5Awrite(aid, tid, &digChanAttr[1]);
                ret = H5Aclose(aid);
            }
            ret = H5Tclose(tid);
            ret = H5Gclose(gid);
        }

        ret = H5Gclose(gid_channel);
    }

    bool bHasVideo = false;
    // Add video group
    if (nVer >= 23)
    {
        hid_t gid_video = H5Gcreate(file, "video", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        hid_t tid_tracking_attr = CreateTrackingAttrType(gid_video);
        hid_t tid_synch_attr = CreateSynchAttrType(gid_video);
        g_tid_synch = CreateSynchType(gid_video);
        // Add synchronization groups
        if (synchAttr.szLabel != NULL)
        {
            bHasVideo = true;
            char szNum[6];
            std::string strLabel = "synch";
            sprintf(szNum, "%05u", synchAttr.id);
            strLabel += szNum;
            hid_t gid = H5Gcreate(gid_video, strLabel.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
            hid_t aid = H5Acreate(gid, "BmiSynch", tid_synch_attr, space_attr, H5P_DEFAULT, H5P_DEFAULT);
            ret = H5Awrite(aid, tid_synch_attr, &synchAttr);
            ret = H5Aclose(aid);
            ret = H5Gclose(gid);
        }
        // Add tracking groups
        for (int i = 0; i < cbMAXTRACKOBJ; ++i)
        {
            g_tid_tracking[i] = -1;
            g_bFixedLengthTracking[i] = false;
            if (trackingAttr[i].szLabel != NULL)
            {
                bHasVideo = true;
                int dim = 2, width = 2;
                switch (trackingAttr[i].type)
                {
                case cbTRACKOBJ_TYPE_2DMARKERS:
                case cbTRACKOBJ_TYPE_2DBLOB:
                case cbTRACKOBJ_TYPE_2DBOUNDARY:
                    dim = 2;
                    width = 2;
                    break;
                case cbTRACKOBJ_TYPE_3DMARKERS:
                    dim = 3;
                    width = 2;
                    break;
                case cbTRACKOBJ_TYPE_1DSIZE:
                    dim = 1;
                    width = 4;
                    break;
                default:
                    // The defualt is already set
                    break;
                }
                // The only fixed length now is the case for single point tracking
                if (trackingAttr[i].maxPoints == 1)
                {
                    g_bFixedLengthTracking[i] = true;
                    g_tid_tracking[i] = CreateTrackingType(gid_video, dim, width,  1);
                } else {
                    g_tid_tracking[i] = CreateTrackingType(gid_video, dim, width);
                }
                char szNum[6];
                std::string strLabel = "tracking";
                sprintf(szNum, "%05u", trackingAttr[i].trackID);
                strLabel += szNum;

                hid_t gid = H5Gcreate(gid_video, strLabel.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
                hid_t aid = H5Acreate(gid, "BmiTracking", tid_tracking_attr, space_attr, H5P_DEFAULT, H5P_DEFAULT);
                ret = H5Awrite(aid, tid_tracking_attr, &trackingAttr[i]);
                ret = H5Aclose(aid);
                ret = H5Gclose(gid);
            } // end if (trackingAttr[i].szLabel
        } // end for (int i = 0
        ret = H5Tclose(tid_tracking_attr);
        ret = H5Tclose(tid_synch_attr);
        ret = H5Gclose(gid_video);
    }
    // Comment group
    if (nVer >= 23)
    {
        hid_t gid_comment = H5Gcreate(file, "comment", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        g_tid_comment = CreateCommentType(gid_comment);
        {
            // NeuroMotive charset is fixed
            hid_t aid = H5Acreate(gid_comment, "NeuroMotiveCharset", H5T_NATIVE_UINT8, space_attr, H5P_DEFAULT, H5P_DEFAULT);
            UINT8 charset = 255;
            ret = H5Awrite(aid, H5T_NATIVE_UINT8, &charset);
            ret = H5Aclose(aid);

            hid_t gid;
            if (bHasVideo)
            {
                gid = H5Gcreate(gid_comment, "comment00256", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
                aid = H5Acreate(gid, "Charset", H5T_NATIVE_UINT8, space_attr, H5P_DEFAULT, H5P_DEFAULT);
                ret = H5Awrite(aid, H5T_NATIVE_UINT8, &charset);
                ret = H5Aclose(aid);
                ret = H5Gclose(gid);
            }
            charset = 0; // Add group for normal comments right here
            gid = H5Gcreate(gid_comment, "comment00001", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
            aid = H5Acreate(gid, "Charset", H5T_NATIVE_UINT8, space_attr, H5P_DEFAULT, H5P_DEFAULT);
            ret = H5Awrite(aid, H5T_NATIVE_UINT8, &charset);
            ret = H5Aclose(aid);
            ret = H5Gclose(gid);
        }
        ret = H5Gclose(gid_comment);
    }
    ret = H5Sclose(space_attr);
    return 0;
}

// Author & Date:   Ehsan Azar   Nov 13, 2012
// Purpose: Convert NEV 
// Inputs:
//  pFile - the source file
//  file  - the destination file
// Outputs:
//   Returns 0 on success, error code otherwise
int ConvertNev(FILE * pFile, hid_t file)
{
    herr_t ret;
    
    NevHdr isHdr;
    if (CreateNevGroups(pFile, file, isHdr))
    {
        return 1;
    }

    // Add packet tables
    {
        size_t chunk_size = CHUNK_SIZE_EVENT;
        int compression = -1; // TODO: use options to add compression
        hid_t ptid_spike[cbNUM_ANALOG_CHANS];
        for (int i = 0; i < cbNUM_ANALOG_CHANS; ++i)
            ptid_spike[i] = -1;
        hid_t ptid_serial = -1, ptid_digital = -1, ptid_synch = -1;
        hid_t ptid_comment[256];
        for (int i = 0; i < 256; ++i)
            ptid_comment[i] = -1;
        hid_t ptid_tracking[cbMAXTRACKOBJ];
        for (int i = 0; i < cbMAXTRACKOBJ; ++i)
            ptid_tracking[i] = -1;
        NevData nevData;
        fseeko(pFile, isHdr.dwStartOfData, SEEK_SET);
        size_t nGot = fread(&nevData, isHdr.dwBytesPerPacket, 1, pFile);
        if (nGot != 1)
        {
            perror("Source file is empty or invalid\n");
            return 1;
        }
        do {

            if (nevData.wPacketID >= 1 && nevData.wPacketID <= cbNUM_ANALOG_CHANS) // found spike data
            {
                int id = nevData.wPacketID; // 1-based
                if (ptid_spike[id - 1] < 0)
                {
                    char szNum[6];
                    std::string strLabel = "/channel/channel";
                    sprintf(szNum, "%05u", id);
                    strLabel += szNum;
                    hid_t gid;
                    if(H5Lexists(file, strLabel.c_str(), H5P_DEFAULT))
                    {
                        gid = H5Gopen(file, strLabel.c_str(), H5P_DEFAULT);
                    } else {
                        printf("Creating %s without attributes\n", strLabel.c_str());
                        gid = H5Gcreate(file, strLabel.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
                    }
                    ptid_spike[id - 1] = H5PTcreate_fl(gid, "spike_set", g_tid_spike, chunk_size, compression);
                    H5Gclose(gid);
                }
                BmiSpike16_t spk;
                spk.dwTimestamp = nevData.dwTimestamp;
                spk.res = nevData.spike.res;
                spk.unit = nevData.spike.unit;
                for (int i = 0; i < g_spikeLength; ++i)
                    spk.wave[i] = nevData.spike.wave[i];
                ret = H5PTappend(ptid_spike[id - 1], 1, &spk);
            } else {
                switch (nevData.wPacketID)
                {
                case 0:      // found a digital or serial event
                    if (!(nevData.digital.byInsertionReason & 1))
                    {
                        // Other digital events are not implemented in NSP yet
                        printf("Unkown digital event (%u) dropped\n", nevData.digital.byInsertionReason);
                        break;
                    }
                    if (nevData.digital.byInsertionReason & 128) // If bit 7 is set it is serial
                    {
                        if (ptid_serial < 0)
                        {
                            hid_t gid = H5Gopen(file, "/channel/serial1", H5P_DEFAULT);
                            ptid_serial = H5PTcreate_fl(gid, "serial_set", g_tid_dig, chunk_size, compression);
                            H5Gclose(gid);
                        }
                        BmiDig16_t dig;
                        dig.dwTimestamp = nevData.dwTimestamp;
                        dig.value = nevData.digital.wDigitalValue;
                        ret = H5PTappend(ptid_serial, 1, &dig);
                    } else {
                        if (ptid_digital < 0)
                        {
                            hid_t gid = H5Gopen(file, "/channel/digital1", H5P_DEFAULT);
                            ptid_digital = H5PTcreate_fl(gid, "digital_set", g_tid_dig, chunk_size, compression);
                            H5Gclose(gid);
                        }
                        BmiDig16_t dig;
                        dig.dwTimestamp = nevData.dwTimestamp;
                        dig.value = nevData.digital.wDigitalValue;
                        ret = H5PTappend(ptid_digital, 1, &dig);
                    }
                    break;
                case 0xFFFF: // found a comment event
                    {
                        int id = nevData.comment.charset; // 0-based
                        if (ptid_comment[id] < 0)
                        {
                            char szNum[6];
                            std::string strLabel = "/comment/comment";
                            sprintf(szNum, "%05u", id + 1);
                            strLabel += szNum;
                            hid_t gid;
                            if(H5Lexists(file, strLabel.c_str(), H5P_DEFAULT))
                            {
                                gid = H5Gopen(file, strLabel.c_str(), H5P_DEFAULT);
                            } else {
                                gid = H5Gcreate(file, strLabel.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
                                {
                                    hsize_t     dims[1] = {1};
                                    hid_t space_attr = H5Screate_simple(1, dims, NULL);
                                    hid_t aid = H5Acreate(gid, "Charset", H5T_NATIVE_UINT8, space_attr, H5P_DEFAULT, H5P_DEFAULT);
                                    UINT8 charset = nevData.comment.charset;
                                    ret = H5Awrite(aid, H5T_NATIVE_UINT8, &charset);
                                    ret = H5Aclose(aid);
                                    ret = H5Sclose(space_attr);
                                }
                            }

                            ptid_comment[id] = H5PTcreate_fl(gid, "comment_set", g_tid_comment, chunk_size, compression);
                            H5Gclose(gid);
                        }
                        BmiComment_t cmt;
                        cmt.dwTimestamp = nevData.dwTimestamp;
                        cmt.data = nevData.comment.data;
                        cmt.flags = nevData.comment.flags;
                        cmt.szComment = nevData.comment.comment;
                        ret = H5PTappend(ptid_comment[id], 1, &cmt);
                    }                    
                    break;
                case 0xFFFE: // found a synchronization event
                    {
                        int id = nevData.synch.id; // 0-based
                        if (id != 0)
                        {
                            printf("Unsupported synchronization source dropped\n");
                            break;
                        }
                        if (ptid_synch < 0)
                        {
                            char szNum[6];
                            std::string strLabel = "/video/synch";
                            sprintf(szNum, "%05u", id + 1);
                            strLabel += szNum;
                            hid_t gid;
                            if(H5Lexists(file, strLabel.c_str(), H5P_DEFAULT))
                            {
                                gid = H5Gopen(file, strLabel.c_str(), H5P_DEFAULT);
                            } else {
                                printf("Creating %s without attributes\n", strLabel.c_str());
                                gid = H5Gcreate(file, strLabel.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
                            }
                            ptid_synch = H5PTcreate_fl(gid, "synch_set", g_tid_synch, chunk_size, compression);
                            H5Gclose(gid);
                        }
                        BmiSynch_t synch;
                        synch.dwTimestamp = nevData.dwTimestamp;
                        synch.etime = nevData.synch.etime;
                        synch.frame = nevData.synch.frame;
                        synch.split = nevData.synch.split;
                        ret = H5PTappend(ptid_synch, 1, &synch);
                    }
                    break;
                case 0xFFFD: // found a video tracking event
                    {
                        int id = nevData.track.nodeID; // 0-based
                        if (id >= cbMAXTRACKOBJ)
                        {
                            printf("Invalid tracking packet dropped\n");
                            break;
                        }
                        if (ptid_tracking[id] < 0)
                        {
                            char szNum[6];
                            std::string strLabel = "/video/tracking";
                            sprintf(szNum, "%05u", id + 1);
                            strLabel += szNum;
                            hid_t gid;
                            if(H5Lexists(file, strLabel.c_str(), H5P_DEFAULT))
                            {
                                gid = H5Gopen(file, strLabel.c_str(), H5P_DEFAULT);
                            } else {
                                printf("Creating %s without attributes\n", strLabel.c_str());
                                gid = H5Gcreate(file, strLabel.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
                            }
                            if (g_tid_tracking[id] < 0)
                            {
                                printf("Creating tracking set with undefined type\n");
                                hid_t gid_video = H5Gopen(file, "/video", H5P_DEFAULT);
                                g_tid_tracking[id] = CreateTrackingType(gid_video, 2, 2);
                                ret = H5Gclose(gid_video);
                            }
                            ptid_tracking[id] = H5PTcreate_fl(gid, "tracking_set", g_tid_tracking[id], chunk_size, compression);
                            H5Gclose(gid);
                        }

                        if (g_bFixedLengthTracking[id])
                        {
                            BmiTracking_fl_t tr;
                            tr.dwTimestamp = nevData.dwTimestamp;
                            tr.nodeCount = nevData.track.nodeCount;
                            tr.parentID = nevData.track.parentID;
                            memcpy(tr.coords, &nevData.track.coords[0], sizeof(tr.coords));
                            ret = H5PTappend(ptid_tracking[id], 1, &tr);
                        } else {
                            BmiTracking_t tr;
                            tr.dwTimestamp = nevData.dwTimestamp;
                            tr.nodeCount = nevData.track.nodeCount;
                            tr.parentID = nevData.track.parentID;
                            tr.coords.len = nevData.track.coordsLength;
                            tr.coords.p = nevData.track.coords;
                            ret = H5PTappend(ptid_tracking[id], 1, &tr);
                        }
                    }
                    break;
                default:
                    if (nevData.wPacketID <= 2048)
                        printf("Unexpected spike channel (%u) dropped\n", nevData.wPacketID);
                    else
                        printf("Unknown packet type (%u) dropped\n", nevData.wPacketID);
                    break;
                }
            }
            // Read more packets
            nGot = fread(&nevData, isHdr.dwBytesPerPacket, 1, pFile);
        } while (nGot == 1);
    }

    //
    // We are going to call H5Close so no need to close what is open at this stage
    //
    return 0;
}

// Author & Date:   Ehsan Azar   Nov 17, 2012
// Purpose: Convert NSx2.1
// Inputs:
//  szSrcFile - source file name
//  pFile     - the source file
//  file      - the destination file
// Outputs:
//   Returns 0 on success, error code otherwise
int ConvertNSx21(const char * szSrcFile, FILE * pFile, hid_t file)
{
    herr_t ret;
    Nsx21Hdr isHdr;
    // Read the header
    fseeko(pFile, 0, SEEK_SET);    // read header from beginning of file
    fread(&isHdr, sizeof(isHdr), 1, pFile);
    
    if (isHdr.cnChannels > cbNUM_ANALOG_CHANS)
    {
        printf("Invalid number of channels in source file header\n");
        return 1;
    }

    BmiChanAttr_t chanAttr[cbNUM_ANALOG_CHANS];
    memset(chanAttr, 0, sizeof(chanAttr));

    // Add root attribute
    if (AddRoot(szSrcFile, pFile, file, isHdr))
        return 1;

    hid_t ptid_chan[cbNUM_ANALOG_CHANS];
    {
        size_t chunk_size = CHUNK_SIZE_CONTINUOUS;
        int compression = -1; // TODO: use options to add compression

        hsize_t     dims[1] = {1};
        hid_t space_attr = H5Screate_simple(1, dims, NULL);

        hid_t gid_channel = H5Gcreate(file, "channel", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        hid_t tid_chan_attr = CreateChanAttrType(gid_channel);
        for (UINT32 i = 0; i < isHdr.cnChannels; ++i)
        {
            UINT32 id; // 1-based
            if (fread(&id, sizeof(UINT32), 1, pFile) != 1)
            {
                printf("Invalid header in source file\n");
                return 1;
            }
            chanAttr[i].id = id;
            chanAttr[i].fClock = 30000;
            // FIXME: This might be incorrect for really old file recordings
            // TODO: search the file to see if 14 is more accurate
            chanAttr[i].nSampleBits = 16;
            chanAttr[i].fSampleRate = float(30000.0) / isHdr.nPeriod;

            char szNum[6];
            std::string strLabel = "channel";
            sprintf(szNum, "%05u", id);
            strLabel += szNum;
            hid_t gid = H5Gcreate(gid_channel, strLabel.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

            // Basic channel attributes
            hid_t aid = H5Acreate(gid, "BmiChan", tid_chan_attr, space_attr, H5P_DEFAULT, H5P_DEFAULT);
            ret = H5Awrite(aid, tid_chan_attr, &chanAttr[i]);
            ret = H5Aclose(aid);


            ptid_chan[i] = H5PTcreate_fl(gid, "continuous_set", H5T_NATIVE_INT16, chunk_size, compression);

            hid_t dsid = H5Dopen(gid, "continuous_set", H5P_DEFAULT);
            ret = H5Gclose(gid);
            aid = H5Acreate(dsid, "Offset", H5T_NATIVE_UINT32, space_attr, H5P_DEFAULT, H5P_DEFAULT);
            UINT32 nOffset = 0; // 2.1 does not have paused headers
            ret = H5Awrite(aid, H5T_NATIVE_UINT32, &nOffset);
            ret = H5Aclose(aid);
#if 0
            hid_t dapl = H5Dget_access_plist(dsid);
            size_t rdcc_nslots, rdcc_nbytes;
            double rdcc_w0;
            ret =  H5Pget_chunk_cache(dapl, &rdcc_nslots, &rdcc_nbytes, &rdcc_w0);
#endif
            ret = H5Dclose(dsid);
        } // end for (UINT32 i = 0
        ret = H5Tclose(tid_chan_attr);
        ret = H5Gclose(gid_channel);
        ret = H5Sclose(space_attr);
    }

    int count = 0;
    INT16 anDataBufferCache[cbNUM_ANALOG_CHANS][CHUNK_SIZE_CONTINUOUS];
    INT16 anDataBuffer[cbNUM_ANALOG_CHANS];
    size_t nGot = fread(anDataBuffer, sizeof(INT16), isHdr.cnChannels, pFile);
    if (nGot != isHdr.cnChannels)
    {
        perror("Source file is empty or invalid\n");
        return 1;
    }
    do {
        for (UINT32 i = 0; i < isHdr.cnChannels; ++i)
        {
            anDataBufferCache[i][count] = anDataBuffer[i];
        }
        count++;
        if (count == CHUNK_SIZE_CONTINUOUS)
        {
            for (UINT32 i = 0; i < isHdr.cnChannels; ++i)
            {
                ret = H5PTappend(ptid_chan[i], count, &anDataBufferCache[i][0]);
            }
            count = 0;
        }
        nGot = fread(anDataBuffer, sizeof(INT16), isHdr.cnChannels, pFile);
    } while (nGot == isHdr.cnChannels);

    // Write out the remaining chunk
    if (count > 0)
    {
        for (UINT32 i = 0; i < isHdr.cnChannels; ++i)
        {
            ret = H5PTappend(ptid_chan[i], count, &anDataBufferCache[i][0]);
        }
    }

    //
    // We are going to call H5Close so no need to close what is open at this stage
    //
    return 0;
}

// Author & Date:   Ehsan Azar   Nov 17, 2012
// Purpose: Convert NSx2.2
// Inputs:
//  pFile - the source file
//  file  - the destination file
// Outputs:
//   Returns 0 on success, error code otherwise
int ConvertNSx22(FILE * pFile, hid_t file)
{
    herr_t ret;
    Nsx22Hdr isHdr;
    // Read the header
    fseeko(pFile, 0, SEEK_SET);    // read header from beginning of file
    fread(&isHdr, sizeof(isHdr), 1, pFile);
    UINT64 dataStart = isHdr.nBytesInHdrs + sizeof(Nsx22DataHdr);

    if (isHdr.cnChannels > cbNUM_ANALOG_CHANS)
    {
        printf("Invalid number of channels in source file header\n");
        return 1;
    }

    BmiChanAttr_t chanAttr[cbNUM_ANALOG_CHANS];
    memset(chanAttr, 0, sizeof(chanAttr));
    BmiChanExtAttr_t chanExtAttr[cbNUM_ANALOG_CHANS];
    memset(chanExtAttr, 0, sizeof(chanExtAttr));
    BmiChanExt2Attr_t chanExt2Attr[cbNUM_ANALOG_CHANS];
    memset(chanExt2Attr, 0, sizeof(chanExt2Attr));

    // Add root attribute
    if (AddRoot(pFile, file, isHdr))
        return 1;

    // Read extra headers
    for (UINT32 i = 0; i < isHdr.cnChannels; ++i)
    {
        Nsx22ExtHdr isExtHdr;
        if (fread(&isExtHdr, sizeof(isExtHdr), 1, pFile) != 1)
        {
            printf("Invalid source file header\n");
            return 1;
        }
        if (0 != strncmp(isExtHdr.achExtHdrID, "CC", sizeof(isExtHdr.achExtHdrID)))
        {
            printf("Invalid source file extended header\n");
            return 1;
        }
        int id = isExtHdr.id;
        if (id == 0 || id > cbNUM_ANALOG_CHANS)
        {
            printf("Invalid channel ID in source file header\n");
            return 1;
        }
        chanAttr[i].id = isExtHdr.id;
        chanAttr[i].fClock = float(isHdr.nResolution);
        chanAttr[i].fSampleRate = float(isHdr.nResolution) / float(isHdr.nPeriod);
        chanAttr[i].nSampleBits = 16;

        chanExtAttr[i].phys_connector = isExtHdr.phys_connector;
        chanExtAttr[i].connector_pin = isExtHdr.connector_pin;
        UINT64 anarange = INT64(isExtHdr.anamax) - INT64(isExtHdr.anamin);
        UINT64 digrange = INT64(isExtHdr.digmax) - INT64(isExtHdr.digmin);
        if (strncmp(isExtHdr.anaunit, "uV", 2) == 0)
        {
            chanExtAttr[i].dFactor = UINT32((anarange * INT64(1E3)) / digrange);
        }
        else if (strncmp(isExtHdr.anaunit, "mV", 2) == 0)
        {
            chanExtAttr[i].dFactor = UINT32((anarange * INT64(1E6)) / digrange);
        }
        else if (strncmp(isExtHdr.anaunit, "V", 2) == 0)
        {
            chanExtAttr[i].dFactor = UINT32((anarange * INT64(1E9)) / digrange);
        } else {
            printf("Unknown analog unit for channel %u, uV used\n", isExtHdr.id);
            chanExtAttr[i].dFactor = UINT32((anarange * INT64(1E3)) / digrange);
        }
        chanExtAttr[i].filter.hpfreq = isExtHdr.hpfreq;
        chanExtAttr[i].filter.hporder = isExtHdr.hporder;
        chanExtAttr[i].filter.hptype = isExtHdr.hptype;
        chanExtAttr[i].filter.lpfreq = isExtHdr.lpfreq;
        chanExtAttr[i].filter.lporder = isExtHdr.lporder;
        chanExtAttr[i].filter.lptype = isExtHdr.lptype;
        chanExtAttr[i].szLabel = _strdup(isExtHdr.label);

        chanExt2Attr[i].anamax = isExtHdr.anamax;
        chanExt2Attr[i].anamin = isExtHdr.anamin;
        chanExt2Attr[i].digmax = isExtHdr.digmax;
        chanExt2Attr[i].digmin = isExtHdr.digmin;
        strncpy(chanExt2Attr[i].anaunit, isExtHdr.anaunit, 16);
    }

    hsize_t     dims[1] = {1};
    hid_t space_attr = H5Screate_simple(1, dims, NULL);

    hid_t ptid_chan[cbNUM_ANALOG_CHANS];
    {
        hid_t gid_channel = H5Gcreate(file, "channel", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        hid_t tid_chan_attr = CreateChanAttrType(gid_channel);
        hid_t tid_chanext_attr = CreateChanExtAttrType(gid_channel);
        hid_t tid_chanext2_attr = CreateChanExt2AttrType(gid_channel);
        for (UINT32 i = 0; i < isHdr.cnChannels; ++i)
        {
            int id = chanAttr[i].id;
            char szNum[6];
            std::string strLabel = "channel";
            sprintf(szNum, "%05u", id);
            strLabel += szNum;
            hid_t gid = H5Gcreate(gid_channel, strLabel.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

            // Basic channel attributes
            hid_t aid = H5Acreate(gid, "BmiChan", tid_chan_attr, space_attr, H5P_DEFAULT, H5P_DEFAULT);
            ret = H5Awrite(aid, tid_chan_attr, &chanAttr[i]);
            ret = H5Aclose(aid);

            // Extra header attribute
            aid = H5Acreate(gid, "BmiChanExt", tid_chanext_attr, space_attr, H5P_DEFAULT, H5P_DEFAULT);
            ret = H5Awrite(aid, tid_chanext_attr, &chanExtAttr[i]);
            ret = H5Aclose(aid);

            // Additional extra channel attributes
            aid = H5Acreate(gid, "BmiChanExt2", tid_chanext2_attr, space_attr, H5P_DEFAULT, H5P_DEFAULT);
            ret = H5Awrite(aid, tid_chanext2_attr, &chanExt2Attr[i]);
            ret = H5Aclose(aid);

            ret = H5Gclose(gid);
        } // end for (UINT32 i = 0
        ret = H5Tclose(tid_chanext2_attr);
        ret = H5Tclose(tid_chanext_attr);
        ret = H5Tclose(tid_chan_attr);
        ret = H5Gclose(gid_channel);
    }

    // Now read data
    fseeko(pFile, isHdr.nBytesInHdrs, SEEK_SET);
    Nsx22DataHdr isDataHdr;
    size_t nGot = fread(&isDataHdr, sizeof(Nsx22DataHdr), 1, pFile);
    int setCount = 0;
    if (nGot != 1)
    {
        printf("Invalid source file (cannot read data header)\n");
        return 1;
    }
    do {
        if (isDataHdr.nHdr != 1)
        {
            printf("Invalid data header in source file\n");
            break;
        }
        for (UINT32 i = 0; i < isHdr.cnChannels; ++i)
        {
            size_t chunk_size = CHUNK_SIZE_CONTINUOUS;
            int compression = -1; // TODO: use options to add compression
            char szNum[6];
            std::string strLabel = "/channel/channel";
            sprintf(szNum, "%05u", chanAttr[i].id);
            strLabel += szNum;
            hid_t gid = H5Gopen(file, strLabel.c_str(), H5P_DEFAULT);
            strLabel = "continuous_set";
            if (setCount > 0)
            {
                sprintf(szNum, "%05u", setCount);
                strLabel += szNum;
            }
            ptid_chan[i] = H5PTcreate_fl(gid, strLabel.c_str(), H5T_NATIVE_INT16, chunk_size, compression);

            hid_t dsid = H5Dopen(gid, strLabel.c_str(), H5P_DEFAULT);
            ret = H5Gclose(gid);
            hid_t aid = H5Acreate(dsid, "Offset", H5T_NATIVE_UINT32, space_attr, H5P_DEFAULT, H5P_DEFAULT);
            UINT32 nOffset = isDataHdr.nTimestamp;
            ret = H5Awrite(aid, H5T_NATIVE_UINT32, &nOffset);
            ret = H5Aclose(aid);
            ret = H5Dclose(dsid);
        }
        int count = 0;
        INT16 anDataBufferCache[cbNUM_ANALOG_CHANS][CHUNK_SIZE_CONTINUOUS];
        for (UINT32 i = 0; i < isDataHdr.nNumDatapoints; ++i)
        {
            INT16 anDataBuffer[cbNUM_ANALOG_CHANS];
            size_t nGot = fread(anDataBuffer, sizeof(INT16), isHdr.cnChannels, pFile);
            if (nGot != isHdr.cnChannels)
            {
                printf("Fewer data points than specified in data header at the source file");
                break;
            }
            for (UINT32 j = 0; j < isHdr.cnChannels; ++j)
            {
                anDataBufferCache[j][count] = anDataBuffer[j];
            }
            count++;
            if (count == CHUNK_SIZE_CONTINUOUS)
            {
                for (UINT32 j = 0; j < isHdr.cnChannels; ++j)
                {
                    ret = H5PTappend(ptid_chan[j], count, &anDataBufferCache[j][0]);
                }
                count = 0;
            }
        } // end for (UINT32 i = 0

        // Write out the remaining chunk
        if (count > 0)
        {
            for (UINT32 i = 0; i < isHdr.cnChannels; ++i)
            {
                ret = H5PTappend(ptid_chan[i], count, &anDataBufferCache[i][0]);
            }
        }
        // Close packet tables as we may open them again for paused files
        for (UINT32 i = 0; i < isHdr.cnChannels; ++i)
        {
            ret = H5PTclose(ptid_chan[i]);
        }        
        // Read possiblly more data streams
        nGot = fread(&isDataHdr, sizeof(Nsx22DataHdr), 1, pFile);
        setCount++;
    } while (nGot == 1);

    //
    // We are going to call H5Close so no need to close what is open at this stage
    //
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////
// Function main()
/////////////////////////////////////////////////////////////////////////////////////////////////
int main(int argc, char * const argv[])
{
    herr_t ret;
    int idxSrcFile = 1;
    bool bForce = false;
    bool bCache = true;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--force") == 0)
        {
            bForce = true;
            idxSrcFile++;
        }
        else if (strcmp(argv[i], "--nocache") == 0)
        {
            bCache = false;
            idxSrcFile++;
        }
    }
    if (idxSrcFile >= argc)
    {
        printf("Blackrock file conversion utility (version 1.0)\n"
               "Usage: n2h5 [options] <srcfile> [<destfile>]\n"
               "Purpose: Converts srcfile to destfile\n"
               "Inputs:\n"
               "<srcfile>  - the file to convert from (nev or nsx format)\n"
               "<destfile> - the converted file to create (hdf5 format)\n"
               "             default is <srcfile>.bh5\n"
               "Options:\n"
               " --force : overwrites the destination if it exists\n"
               " --nocache : slower but results in smaller file size\n");
        return 0;
    }
    const char * szSrcFile = argv[idxSrcFile];
    std::string strDest;
    if ((idxSrcFile + 1) >= argc)
    {
        strDest = szSrcFile;
        strDest += ".bh5";
    } else {
        strDest = argv[idxSrcFile + 1];
    }
    const char * szDstFile = strDest.c_str();

    char achFileID[8];

    FILE * pFile = fopen(szSrcFile, "rb");
    if (pFile == NULL)
    {
        perror("Unable to open source file for reading");
        return 0;
    }

    if (H5open())
    {
        fclose(pFile);
        printf("cannot open hdf5 library\n");
        return 0;
    }

    hid_t facpl = H5P_DEFAULT;
    if (bCache)
    {
        double rdcc_w0 = 1; // We only write so this should work
        facpl = H5Pcreate(H5P_FILE_ACCESS);
        // Useful primes: 401 4049 404819
        ret = H5Pset_cache(facpl, 0, 404819, 4 * 1024 * CHUNK_SIZE_CONTINUOUS, rdcc_w0);
    }
    hid_t file;
    file = H5Fcreate(szDstFile, bForce ? H5F_ACC_TRUNC : H5F_ACC_EXCL, H5P_DEFAULT, facpl);
    if (facpl != H5P_DEFAULT)
        ret = H5Pclose(facpl);

    if (file < 0)
    {
        printf("Cannot create destination file or destiantion file exists\n"
               "Use --force to overwite the file\n");
        goto ErrHandle;
    }
    fread(&achFileID, sizeof(achFileID), 1, pFile);
    // NEV file
    if (0 == strncmp(achFileID, "NEURALEV", sizeof(achFileID)))
    {
        if (ConvertNev(pFile, file))
        {
            printf("Error in ConvertNev()\n");
            goto ErrHandle;
        }
    }
    // 2.1 filespec
    else if (0 == strncmp(achFileID, "NEURALSG", sizeof(achFileID)))
    {
        if (ConvertNSx21(szSrcFile, pFile, file))
        {
            printf("Error in ConvertNSx21()\n");
            goto ErrHandle;
        }
    }
    // 2.2 filespec
    else if (0 == strncmp(achFileID, "NEURALCD", sizeof(achFileID)))
    {
        if (ConvertNSx22(pFile, file))
        {
            printf("Error in ConvertNSx22()\n");
            goto ErrHandle;
        }
    } else {
        printf("Invalid source file format\n");
    }
ErrHandle:
    if (pFile)
        fclose(pFile);
    if (file > 0)
        H5Fclose(file);
    H5close();
    return 0;
}