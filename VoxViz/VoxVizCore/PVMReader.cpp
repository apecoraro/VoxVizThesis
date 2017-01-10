#include "VoxVizCore/PVMReader.h"

#include <algorithm>

using namespace vox;

PVMReader& PVMReader::instance()
{
    static PVMReader s_instance;
    return s_instance;
}

static unsigned char *ReadPVMVolume(const char *filename,
                                    unsigned int *width,unsigned int *height,unsigned int *depth,unsigned int *components,
                                    float *scalex,float *scaley,float *scalez,
                                    unsigned char **description,
                                    unsigned char **courtesy,
                                    unsigned char **parameter,
                                    unsigned char **comment);

static unsigned char *Quantize16to8(unsigned char *volume,
                                    unsigned int width,unsigned int height,unsigned int depth,
                                    int linear=0);

VolumeDataSet* PVMReader::readVolumeData(const std::string& inputFile)
{
    unsigned int width, height, depth, comps;
    float scaleX, scaleY, scaleZ;
    unsigned char *desc, *ctsy, *param, *cmnt;
    unsigned char* pVoxelData = ReadPVMVolume(inputFile.c_str(), 
                                              &width, &height, &depth, 
                                              &comps, 
                                              &scaleX, &scaleY, &scaleZ, 
                                              &desc, &ctsy, &param, &cmnt);

    if(pVoxelData == NULL)
        return NULL;

    if(comps == 2)
    {
        pVoxelData = Quantize16to8(pVoxelData,
                                   width, height, depth);
                                   
                                   
    }
    else if(comps != 1)
    {
        free(pVoxelData);
        return NULL;
    }

    QVector3D pos(0, 0, 0);//default position and orientation
    QQuaternion orient = QQuaternion::fromAxisAndAngle(0, 0, 1, 0);
    VolumeDataSet* pVolume = 
          new VolumeDataSet(inputFile, 
                            pos, 
                            orient,
                            scaleX,
                            scaleY,
                            scaleZ,
                            width, 
                            height,
                            depth);

    pVolume->setData(pVoxelData);

    return pVolume;
}

// The following code originally written by Stefan Roettger as part of
// V3 viewer

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define DDS_MAXSTR (256)

#define DDS_BLOCKSIZE (1<<20)
#define DDS_INTERLEAVE (1<<24)

#define DDS_RL (7)

#define DDS_ISINTEL (*((unsigned char *)(&DDS_INTEL)+1)==0)

FILE *DDS_file;

char DDS_ID[]="DDS v3d\n";
char DDS_ID2[]="DDS v3e\n";

unsigned int DDS_buffer;
int DDS_bufsize, DDS_bitcnt;

unsigned short int DDS_INTEL=1;

inline unsigned int DDS_shiftl(const unsigned int value,const int bits)
{
    return((bits>=32) ? 0 : value << bits);
}

inline unsigned int DDS_shiftr(const unsigned int value,const int bits)
{
    return((bits>=32) ? 0:value >> bits);
}

static void InitBuffer()
{
    DDS_buffer=0;
    DDS_bufsize=0;
    DDS_bitcnt=0;
}

void DDS_swapuint(unsigned int *x)
{
    unsigned int tmp=*x;

    *x=((tmp&0xff)<<24)|
        ((tmp&0xff00)<<8)|
        ((tmp&0xff0000)>>8)|
        ((tmp&0xff000000)>>24);
}

static inline void PrintError()
{
    fprintf(stderr,"fatal error in <%s> at line %d!\n", __FILE__, __LINE__);
    exit(EXIT_FAILURE);
}

static unsigned int ReadBits(FILE *file,int bits)
{
    unsigned int value;

    if (bits<0 || bits>32) PrintError();

    if (bits==0) return(0);

    if (bits<DDS_bufsize)
    {
        DDS_bufsize-=bits;
        value=DDS_shiftr(DDS_buffer,DDS_bufsize);
    }
    else
    {
        value=DDS_shiftl(DDS_buffer,bits-DDS_bufsize);
        DDS_buffer=0;
        fread(&DDS_buffer,1,4,file);
        if (DDS_ISINTEL) 
            DDS_swapuint(&DDS_buffer);

        DDS_bufsize+=32-bits;
        value|=DDS_shiftr(DDS_buffer,DDS_bufsize);
    }

    DDS_buffer&=DDS_shiftl(1,DDS_bufsize)-1;
    DDS_bitcnt+=bits;

    return(value);
}

static inline int DDS_decode(int bits)
{
    return(bits>=1?bits+1:bits);
}

#ifndef TRUE
    #define TRUE 1
#endif

#ifndef FALSE
    #define FALSE 0
#endif

static void deinterleave(unsigned char *data,unsigned int bytes,unsigned int skip,unsigned int block=0, int restore=FALSE)
{
    unsigned int i,j,k;

    unsigned char *data2,*ptr;

    if (skip<=1) return;

    if (block==0)
    {
        if ((data2=(unsigned char *)malloc(bytes))==NULL) PrintError();

        if (!restore)
            for (ptr=data2,i=0; i<skip; i++)
            for (j=i; j<bytes; j+=skip) *ptr++=data[j];
        else
            for (ptr=data,i=0; i<skip; i++)
            for (j=i; j<bytes; j+=skip) data2[j]=*ptr++;

        memcpy(data,data2,bytes);
    }
    else
    {
        if ((data2=(unsigned char *)malloc((bytes<skip*block)?bytes:skip*block))==NULL) PrintError();

        if (!restore)
        {
            for (k=0; k<bytes/skip/block; k++)
            {
                for (ptr=data2,i=0; i<skip; i++)
                    for (j=i; j<skip*block; j+=skip) *ptr++=data[k*skip*block+j];

                memcpy(data+k*skip*block,data2,skip*block);
            }

            for (ptr=data2,i=0; i<skip; i++)
                for (j=i; j<bytes-k*skip*block; j+=skip) *ptr++=data[k*skip*block+j];

            memcpy(data+k*skip*block,data2,bytes-k*skip*block);
        }
        else
        {
            for (k=0; k<bytes/skip/block; k++)
            {
                for (ptr=data+k*skip*block,i=0; i<skip; i++)
                    for (j=i; j<skip*block; j+=skip) data2[j]=*ptr++;

                memcpy(data+k*skip*block,data2,skip*block);
            }

            for (ptr=data+k*skip*block,i=0; i<skip; i++)
                for (j=i; j<bytes-k*skip*block; j+=skip) data2[j]=*ptr++;

            memcpy(data+k*skip*block,data2,bytes-k*skip*block);
        }
    }

    free(data2);
}
// interleave a byte stream
void interleave(unsigned char *data,unsigned int bytes,unsigned int skip,unsigned int block=0)
{
    deinterleave(data,bytes,skip,block,TRUE);
}

static unsigned char *ReadDDSfile(const char *filename,unsigned int *bytes)
{
    int version=1;

    unsigned int skip,strip;

    unsigned char *data,*ptr;

    unsigned int cnt,cnt1,cnt2;
    int bits,act;

    if ((DDS_file=fopen(filename,"rb"))==NULL) return(NULL);

    for (cnt=0; DDS_ID[cnt]!='\0'; cnt++)
    {
        if (fgetc(DDS_file)!=DDS_ID[cnt])
        {
            fclose(DDS_file);
            version=0;
            break;
        }
    }

    if (version==0)
    {
        if ((DDS_file=fopen(filename,"rb"))==NULL) return(NULL);

        for (cnt=0; DDS_ID2[cnt]!='\0'; cnt++)
        {
            if (fgetc(DDS_file)!=DDS_ID2[cnt])
            {
                fclose(DDS_file);
                return(NULL);
            }
        }

        version=2;
    }

    InitBuffer();

    skip=ReadBits(DDS_file,2)+1;
    strip=ReadBits(DDS_file,16)+1;

    data=ptr=NULL;
    cnt=act=0;

    while ((cnt1=ReadBits(DDS_file,DDS_RL))!=0)
    {
        bits=DDS_decode(ReadBits(DDS_file,3));

        for (cnt2=0; cnt2<cnt1; cnt2++)
        {
            if (cnt<=strip) act+=ReadBits(DDS_file,bits)-(1<<bits)/2;
            else act+=*(ptr-strip)-*(ptr-strip-1)+ReadBits(DDS_file,bits)-(1<<bits)/2;

            while (act<0) act+=256;
            while (act>255) act-=256;

            if (cnt%DDS_BLOCKSIZE==0)
            {
                if ((data=(unsigned char *)realloc(data,cnt+DDS_BLOCKSIZE))==NULL) 
                    PrintError();

                ptr=&data[cnt];
            }

            *ptr++=act;
            cnt++;
        }
    }

    fclose(DDS_file);

    if (cnt==0) return(NULL);

    if ((data=(unsigned char *)realloc(data,cnt))==NULL) 
        PrintError();

    if (version==1) 
        interleave(data,cnt,skip);
    else 
        interleave(data,cnt,skip,DDS_INTERLEAVE);

    *bytes=cnt;

    return(data);
}

static unsigned char *ReadPVMVolume(const char *filename,
                                    unsigned int *width,unsigned int *height,unsigned int *depth,unsigned int *components,
                                    float *scalex,float *scaley,float *scalez,
                                    unsigned char **description,
                                    unsigned char **courtesy,
                                    unsigned char **parameter,
                                    unsigned char **comment)
{
    unsigned char *data,*ptr;
    unsigned int bytes,numc;

    int version=1;

    unsigned char *volume;

    float sx=1.0f,sy=1.0f,sz=1.0f;

    unsigned int len1=0,len2=0,len3=0,len4=0;

    if ((data=ReadDDSfile(filename,&bytes))==NULL) 
        return(NULL);
    if (bytes<5) 
        return(NULL);

    if ((data=(unsigned char *)realloc(data,bytes+1))==NULL) 
        PrintError();
    data[bytes]='\0';

    if (strncmp((char *)data,"PVM\n",4)!=0)
    {
        if (strncmp((char *)data,"PVM2\n",5)==0) 
            version=2;
        else if (strncmp((char *)data,"PVM3\n",5)==0) 
            version=3;
        else 
            return(NULL);

        if (sscanf((char *)&data[5],"%d %d %d\n%g %g %g\n",width,height,depth,&sx,&sy,&sz)!=6) 
            PrintError();
        if (*width<1 || *height<1 || *depth<1 || sx<=0.0f || sy<=0.0f || sz<=0.0f) 
            PrintError();
        
        ptr=(unsigned char *)strchr((char *)&data[5],'\n')+1;
    }
    else
    {
        if (sscanf((char *)&data[4],"%d %d %d\n",width,height,depth)!=3) 
            PrintError();
        if (*width<1 || *height<1 || *depth<1) 
            PrintError();
        ptr=&data[4];
    }

    if (scalex!=NULL && scaley!=NULL && scalez!=NULL)
    {
        *scalex=sx;
        *scaley=sy;
        *scalez=sz;
    }

    ptr=(unsigned char *)strchr((char *)ptr,'\n')+1;
    if (sscanf((char *)ptr,"%d\n",&numc)!=1) 
        PrintError();
    if (numc<1) 
        PrintError();

    if (components!=NULL) *components=numc;
    else if (numc!=1) PrintError();

    ptr=(unsigned char *)strchr((char *)ptr,'\n')+1;
    if (version==3) 
        len1=strlen((char *)(ptr+(*width)*(*height)*(*depth)*numc))+1;
    if (version==3) 
        len2=strlen((char *)(ptr+(*width)*(*height)*(*depth)*numc+len1))+1;
    if (version==3) 
        len3=strlen((char *)(ptr+(*width)*(*height)*(*depth)*numc+len1+len2))+1;
    if (version==3) 
        len4=strlen((char *)(ptr+(*width)*(*height)*(*depth)*numc+len1+len2+len3))+1;

    if ((volume=(unsigned char *)malloc((*width)*(*height)*(*depth)*numc+len1+len2+len3+len4))==NULL) 
        PrintError();
    if (data+bytes!=ptr+(*width)*(*height)*(*depth)*numc+len1+len2+len3+len4) 
        PrintError();

    memcpy(volume,ptr,(*width)*(*height)*(*depth)*numc+len1+len2+len3+len4);
    free(data);

    if (description!=NULL)
        if (len1>1) 
            *description=volume+(*width)*(*height)*(*depth)*numc;
        else 
            *description=NULL;

    if (courtesy!=NULL)
        if (len2>1) 
            *courtesy=volume+(*width)*(*height)*(*depth)*numc+len1;
        else 
            *courtesy=NULL;

    if (parameter!=NULL)
        if (len3>1) 
            *parameter=volume+(*width)*(*height)*(*depth)*numc+len1+len2;
        else 
            *parameter=NULL;

    if (comment!=NULL)
        if (len4>1) 
            *comment=volume+(*width)*(*height)*(*depth)*numc+len1+len2+len3;
        else 
            *comment=NULL;

    return(volume);
}

// helper functions for quantize:

inline int DDS_get(unsigned short int *volume,
                   unsigned int width,unsigned int height,unsigned int depth,
                   unsigned int i,unsigned int j,unsigned int k)
{
    return(volume[i+(j+k*height)*width]);
}

inline double DDS_getgrad(unsigned short int *volume,
                          unsigned int width,unsigned int height,unsigned int depth,
                          unsigned int i,unsigned int j,unsigned int k)
{
    double gx,gy,gz;

    if (i>0)
    {
        if (i<width-1) 
            gx=(DDS_get(volume,width,height,depth,i+1,j,k)-DDS_get(volume,width,height,depth,i-1,j,k))/2.0;
        else 
            gx=DDS_get(volume,width,height,depth,i,j,k)-DDS_get(volume,width,height,depth,i-1,j,k);
    }
    else 
        gx=DDS_get(volume,width,height,depth,i+1,j,k)-DDS_get(volume,width,height,depth,i,j,k);

    if (j>0)
    {
        if (j<height-1) 
            gy=(DDS_get(volume,width,height,depth,i,j+1,k)-DDS_get(volume,width,height,depth,i,j-1,k))/2.0;
        else 
            gy=DDS_get(volume,width,height,depth,i,j,k)-DDS_get(volume,width,height,depth,i,j-1,k);
    }
    else 
        gy=DDS_get(volume,width,height,depth,i,j+1,k)-DDS_get(volume,width,height,depth,i,j,k);

    if (k>0)
    {
        if (k<depth-1) 
            gz=(DDS_get(volume,width,height,depth,i,j,k+1)-DDS_get(volume,width,height,depth,i,j,k-1))/2.0;
        else 
            gz=DDS_get(volume,width,height,depth,i,j,k)-DDS_get(volume,width,height,depth,i,j,k-1);
    }
    else 
        gz=DDS_get(volume,width,height,depth,i,j,k+1)-DDS_get(volume,width,height,depth,i,j,k);

    return(sqrt(gx*gx+gy*gy+gz*gz));
}

// quantize 16 bit volume to 8 bit using a non-linear mapping
static unsigned char *Quantize16to8(unsigned char *volume,
                                    unsigned int width,unsigned int height,unsigned int depth,
                                    int linear/*=0*/)
{
    unsigned int i,j,k;

    unsigned char *volume2;
    unsigned short int *volume3;

    int v,vmin,vmax;

    double *err,eint;

    err=new double[65536];

    int done;

    if ((volume3=(unsigned short int*)malloc(width*height*depth*sizeof(unsigned short int)))==NULL) 
        PrintError();

    vmin=vmax=256*volume[0]+volume[1];

    for (k=0; k<depth; k++)
    {
        for (j=0; j<height; j++)
        {
            for (i=0; i<width; i++)
            {
                v=256*volume[2*(i+(j+k*height)*width)]+volume[2*(i+(j+k*height)*width)+1];
                volume3[i+(j+k*height)*width]=v;

                if (v<vmin) 
                    vmin=v;
                else if (v>vmax) 
                    vmax=v;
            }
        }
    }

    free(volume);

    //if (verbose)
    //std::cout << "16 bit volume has scalar range=[" << vmin << "," << vmax << std::endl;

    if (linear)
    {
        for (i=0; i<65536; i++) 
        {
            err[i]=255*(double)i/vmax;
        }
    }
    else
    {
        for (i=0; i<65536; i++) 
            err[i]=0.0;

        for (k=0; k<depth; k++)
        {
            for (j=0; j<height; j++)
            {
                for (i=0; i<width; i++)
                {
                    err[DDS_get(volume3,width,height,depth,i,j,k)]+=sqrt(DDS_getgrad(volume3,width,height,depth,i,j,k));
                }
            }
        }

        for (i=0; i<65536; i++) 
            err[i]=pow(err[i],1.0/3);

        err[vmin]=err[vmax]=0.0;

        for (k=0; k<256; k++)
        {
            for (eint=0.0,i=0; i<65536; i++) 
                eint+=err[i];

            done=TRUE;

            for (i=0; i<65536; i++)
            {
                if (err[i]>eint/256)
                {
                    err[i]=eint/256;
                    done=FALSE;
                }
            }

            if (done) break;
        }

        for (i=1; i<65536; i++) 
            err[i]+=err[i-1];

        if (err[65535]>0.0f)
        {
            for (i=0; i<65536; i++) 
                err[i]*=255.0f/err[65535];
        }
    }

    if ((volume2=(unsigned char *)malloc(width*height*depth))==NULL) 
        PrintError();

    for (k=0; k<depth; k++)
    {            
        for (j=0; j<height; j++)
        {
            for (i=0; i<width; i++)
            {
                volume2[i+(j+k*height)*width]=(int)(err[DDS_get(volume3,width,height,depth,i,j,k)]+0.5);
            }
        }
    }

    delete err;
    free(volume3);

    return(volume2);
}
