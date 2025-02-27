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
package io.glutenproject.vectorized;

import io.netty.util.internal.PlatformDependent;
import org.apache.spark.network.util.LimitedInputStream;
import org.apache.spark.storage.CHShuffleReadStreamFactory;

import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;

public class LowCopyFileSegmentShuffleInputStream implements ShuffleInputStream {

  private final InputStream in;
  private final LimitedInputStream limitedInputStream;
  private final FileChannel channel;
  private final int bufferSize;
  private final boolean isCompressed;

  private long bytesRead = 0L;
  private long left;

  public LowCopyFileSegmentShuffleInputStream(
      InputStream in, InputStream limitedInputStream, int bufferSize, boolean isCompressed) {
    // to prevent underlying netty buffer from being collected by GC
    this.in = in;
    this.limitedInputStream = (LimitedInputStream) limitedInputStream;
    this.bufferSize = bufferSize;
    this.isCompressed = isCompressed;
    final FileInputStream fin;
    try {
      left =
          ((long)
              CHShuffleReadStreamFactory.FIELD_LimitedInputStream_left.get(
                  this.limitedInputStream));
      fin =
          (FileInputStream)
              CHShuffleReadStreamFactory.FIELD_FilterInputStream_in.get(this.limitedInputStream);
    } catch (IllegalAccessException e) {
      throw new RuntimeException(e);
    }
    channel = fin.getChannel();
  }

  @Override
  public long read(long destAddress, long maxReadSize) {
    long bytesToRead = Math.min(left, maxReadSize);
    int bytesToRead32 = Math.toIntExact(bytesToRead);
    if (bytesToRead32 == 0) {
      return 0;
    }
    ByteBuffer direct = PlatformDependent.directBuffer(destAddress, bytesToRead32);
    try {
      // read data from file channel to native address
      int bytes = channel.read(direct);
      if (bytes == -1) {
        return 0;
      }
      bytesRead += bytes;
      left -= bytes;
      return bytes;
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  @Override
  public long pos() {
    return bytesRead;
  }

  @Override
  public boolean isCompressed() {
    return this.isCompressed;
  }

  @Override
  public void close() {
    try {
      channel.close();
      in.close();
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }
}
