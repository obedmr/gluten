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

package org.apache.spark.shuffle

import io.glutenproject.GlutenConfig

import org.apache.spark.SparkConf
import org.apache.spark.internal.config._

import java.util.Locale

object GlutenShuffleUtils {
  def checkCodecValues(codecConf: String, codec: String, validValues: Set[String]): Unit = {
    if (!validValues.contains(codec)) {
      throw new IllegalArgumentException(
        s"The value of $codecConf should be one of " +
          s"${validValues.mkString(", ")}, but was $codec")
    }
  }

  def getCompressionCodec(conf: SparkConf): String = {
    val glutenConfig = GlutenConfig.getConf
    glutenConfig.columnarShuffleCodec match {
      case Some(codec) =>
        val glutenCodecKey = GlutenConfig.COLUMNAR_SHUFFLE_CODEC.key
        if (glutenConfig.columnarShuffleEnableQat) {
          checkCodecValues(glutenCodecKey, codec, GlutenConfig.GLUTEN_QAT_SUPPORTED_CODEC)
          GlutenConfig.GLUTEN_QAT_CODEC_PREFIX + codec
        } else if (glutenConfig.columnarShuffleEnableIaa) {
          checkCodecValues(glutenCodecKey, codec, GlutenConfig.GLUTEN_IAA_SUPPORTED_CODEC)
          GlutenConfig.GLUTEN_IAA_CODEC_PREFIX + codec
        } else {
          codec
        }
      case None =>
        val sparkCodecKey = IO_COMPRESSION_CODEC.key
        val codec =
          conf.get(sparkCodecKey, IO_COMPRESSION_CODEC.defaultValueString).toUpperCase(Locale.ROOT)
        checkCodecValues(sparkCodecKey, codec, GlutenConfig.GLUTEN_SHUFFLE_SUPPORTED_CODEC)
        codec
    }
  }
}
