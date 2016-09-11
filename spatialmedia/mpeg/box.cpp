/*****************************************************************************
 * 
 * Copyright 2016 Varol Okan. All rights reserved.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 ****************************************************************************/

// Tool for loading mpeg4 files and manipulating atoms.
#include <string.h>
#include <iostream>

#include "constants.h"
#include "box.h"

Box::Box ( )
{
  memset ( (char *)m_name, ' ', sizeof ( m_name ) );
  m_iType         = constants::Box;
  m_iPosition     = -1;
  m_iHeaderSize   = 0;
  m_iContentSize  = 0;
  m_pContents     = NULL;
}

Box::~Box ( )
{
  if ( m_pContents )
    delete m_pContents;
  m_pContents = NULL;
  m_iContentSize = m_iHeaderSize = m_iPosition = 0;
}

uint8_t Box::readUint8 ( std::fstream &fs )
{
  union {
    uint8_t iVal;
    char bytes[1];
  } buf;
  fs.read ( buf.bytes, 1 );
  // BigEndian to Host
  return buf.iVal; //be8toh ( buf.iVal );
}

uint32_t Box::readUint32 ( std::fstream &fs )
{
  union {
    uint32_t iVal;
    char bytes[4];
  } buf;
  fs.read ( buf.bytes, 4 );
  // BigEndian to Host
  return be32toh ( buf.iVal );
}

uint64_t Box::readUint64 ( std::fstream &fs )
{
  union {
    uint64_t iVal;
    char bytes[8];
  } buf;
  fs.read ( buf.bytes, 8 );
  // BigEndian to Host
  return be64toh ( buf.iVal );
}

void Box::writeUint8 ( std::fstream &fs, uint8_t iVal )
{
  union {
    uint8_t iVal;
    char bytes[1];
  } buf;
  buf.iVal = iVal; // htobe8 ( iVal );
  fs.write ( (char *)buf.bytes, 1 );
}

void Box::writeUint32 ( std::fstream &fs, uint32_t iVal )
{
  union {
    uint32_t iVal;
    char bytes[4];
  } buf;
  buf.iVal = htobe32 ( iVal );
  fs.write ( (char *)buf.bytes, 4 );
}

void Box::writeUint64 ( std::fstream &fs, uint64_t iVal )
{
  union {
    uint64_t iVal;
    char bytes[8];
  } buf;
  buf.iVal = htobe32 ( iVal );
  fs.write ( (char *)buf.bytes, 8 );
}

Box *Box::load ( std::fstream &fs, uint32_t iPos, uint32_t iEnd )
{
  // Loads the box located at a position in a mp4 file
  //
  if ( iPos < 1 )
       iPos = fs.tellg ( );

  uint32_t iHeaderSize, tmp = 0;
  uint64_t iSize = 0LL;
  char name[4];

  fs.seekg ( iPos );
  iHeaderSize = 8;
  iSize = (uint64_t)readUint32 ( fs );
  fs.read  ( name, 4 );

  if ( iSize == 1 )  {
    iSize = readUint64 ( fs );
    iHeaderSize = 16;
  }
  if ( iSize < 8 )  { 
    std::cerr << "Error, invalid size " << iSize << " in " << name << " at " << iPos << std::endl;
    return NULL;
  }

  if ( iPos + iSize > iEnd )  {
    std::cerr << "Error: Leaf box size exceeds bounds." << std::endl;
    return NULL;
  }
  Box *pNewBox = new Box ( );
  memcpy ( pNewBox->m_name, name, sizeof ( name ) );
  pNewBox->m_iPosition    = iPos;
  pNewBox->m_iHeaderSize  = iHeaderSize;
  pNewBox->m_iContentSize = iSize - iHeaderSize;
  pNewBox->m_pContents = NULL;
  return pNewBox;
}

int32_t Box::type ( )
{
  return m_iType;
}

void Box::clear ( std::vector<Box *> &list )
{
  std::vector<Box *>::iterator it = list.begin ( );
  while (it != list.end ( ) )  {
    delete *it++;
  }

  list.clear ( );
}

int32_t Box::content_start ( )
{
  return m_iPosition + m_iHeaderSize;
}

void Box::save ( std::fstream &fsIn, std::fstream &fsOut, int32_t iDelta )
{
  // Save box contents prioritizing set contents.
  // iDelta = index update amount
  if ( m_iHeaderSize == 16 )  {
    uint64_t iBigSize = htobe64 ( (uint64_t)size ( ) );
    writeUint32 ( fsOut, 1 );
    fsOut.write ( m_name,4 );
    writeUint64 ( fsOut, iBigSize );
  }
  else if ( m_iHeaderSize == 8 )  {
    writeUint32 ( fsOut, size ( ) );
    fsOut.write ( m_name,  4 );
  }
  if ( content_start ( ) )
    fsIn.seekg ( content_start ( ) );

  if ( memcmp ( m_name, constants::TAG_STCO, sizeof ( constants::TAG_STCO ) ) != 0 )
    stco_copy ( fsIn, fsOut, this, iDelta );
  else if ( memcmp ( m_name, constants::TAG_CO64, sizeof ( constants::TAG_CO64 ) ) != 0 )
    co64_copy ( fsIn, fsOut, this, iDelta );
  else if ( m_pContents )
    fsOut.write ( (char *)m_pContents, m_iContentSize );
  else
    tag_copy ( fsIn, fsOut, m_iContentSize );
}

void Box::set ( uint8_t *pNewContents, uint32_t iSize )
{
  // sets / overwrites the box contents.
  m_pContents = pNewContents;
  m_iContentSize = iSize;
}

int32_t Box::size ( )
{
  return m_iHeaderSize + m_iContentSize;
}

void Box::print_structure ( const char *pIndent )
{
  std::cout << "{" << pIndent << "}" << "{" << m_name << "} ";
  std::cout << "[{" << m_iHeaderSize << "}, {" << m_iContentSize << "}]" << std::endl; 
}

void Box::tag_copy ( std::fstream &fsIn, std::fstream &fsOut, int32_t iSize )
{
  // Copies a block of data from fsIn to fsOut.

  //  On 32-bit systems reading / writing is limited to 2GB chunks.
  //  To prevent overflow, read/write 64 MB chunks.
  int32_t block_size = 64 * 1024 * 1024;
  while ( iSize > block_size )  {
    m_pContents = new uint8_t[iSize];
    fsIn.read   ( (char *)m_pContents, block_size );
    fsOut.write ( (char *)m_pContents, block_size );
    iSize = iSize - block_size;
    fsIn.read   ( (char *)m_pContents, iSize );
    fsOut.write ( (char *)m_pContents, iSize );
    // contents = in_fh.read(size)
    // out_fh.write(contents)
  }
}

void Box::index_copy ( std::fstream &fsIn, std::fstream &fsOut, Box *pBox, bool bBigMode, int32_t iDelta )
{
  // Update and copy index table for stco/co64 files.
  // pBox: box, stco/co64 box to copy.
  // bBigMode: if true == BigEndian Uint64, else BigEndian Int32
  // iDelta: int, offset change for index entries.
  std::fstream &fs = fsIn;
  if ( ! pBox->m_pContents )
    fs.seekg ( pBox->content_start ( ) );
  else  {
    //fs = StringIO.StringIO ( box->m_pContents );
    return index_copy_from_contents ( fsOut, pBox, bBigMode, iDelta );
  }

  uint32_t iHeader = readUint32 ( fs );
  uint32_t iValues = readUint32 ( fs );
  writeUint32 ( fsOut, iHeader );
  writeUint32 ( fsOut, iValues );
  if ( bBigMode )  {
   for ( int i=0; i<iValues; i++ )  {
      uint64_t iVal = readUint64 ( fsIn ) + iDelta;
      writeUint64 ( fsOut, iVal );
    }
  }
  else  {
    for ( int i=0; i<iValues; i++ )  {
      uint32_t iVal = readUint32 ( fsIn ) + iDelta;
      writeUint32 ( fsOut, iVal );
    }
  }
}

uint32_t Box::uint32FromCont ( int32_t &iIDX )
{
  union {
    uint32_t iVal;
    char bytes[4];
  } buf;
  memcpy ( buf.bytes, (char *)&m_pContents[iIDX], 4 );
  iIDX += 4;
  return be32toh ( buf.iVal );
}

uint64_t Box::uint64FromCont ( int32_t &iIDX )
{
  union {
    uint64_t iVal;
    char bytes[8];
  } buf;
  memcpy ( buf.bytes, (char *)&m_pContents[iIDX], 8 );
  iIDX += 8;
  return be64toh ( buf.iVal );
}

void Box::index_copy_from_contents ( std::fstream &fsOut, Box *pBox, bool bBigMode, int32_t iDelta )
{
  int32_t iIDX = 0;

  uint32_t iHeader = uint32FromCont ( iIDX );
  uint32_t iValues = uint32FromCont ( iIDX );
  writeUint32 ( fsOut, iHeader );
  writeUint32 ( fsOut, iValues );
  if ( bBigMode )  {
   for ( int i=0; i<iValues; i++ )  {
      uint64_t iVal = uint64FromCont ( iIDX ) + iDelta;
      writeUint64 ( fsOut, iVal );
    }
  }
  else  {
    for ( int i=0; i<iValues; i++ )  {
      uint32_t iVal = uint32FromCont ( iIDX ) + iDelta;
      writeUint32 ( fsOut, iVal );
    }
  }
}

void Box::stco_copy  ( std::fstream &fsIn, std::fstream &fsOut, Box *pBox, int32_t iDelta )
{
  // Copy for stco box.
  index_copy ( fsIn, fsOut, pBox, false, iDelta );
}

void Box::co64_copy  ( std::fstream &fsIn, std::fstream &fsOut, Box *pBox, int32_t iDelta )
{
  // Copy for co64 box.
  index_copy ( fsIn, fsOut, pBox, true, iDelta );
}

