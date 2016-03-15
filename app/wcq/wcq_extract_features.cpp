/*
 * ex_extract_features.cpp
 *
 *  Created on: Dec 9, 2015
 *      Author: wcq
 */
#include <stdio.h>  // for snprintf
#include <string>
#include <vector>
#include <iostream>
#include <fstream>

#include "boost/algorithm/string.hpp"
#include "google/protobuf/text_format.h"

#include "caffe/blob.hpp"
#include "caffe/common.hpp"
#include "caffe/net.hpp"
#include "caffe/proto/caffe.pb.h"
#include "caffe/util/db.hpp"
#include "caffe/util/io.hpp"

using caffe::Blob;
using caffe::Caffe;
using caffe::Datum;
using caffe::Net;
using boost::shared_ptr;
using std::string;
namespace db = caffe::db;

template<typename Dtype>
int feature_extraction_pipeline(std::string pretrained_binary_proto,std::string feature_extraction_proto,
		std::vector<std::string> blob_names,std::vector<std::string> dataset_names,
		int num_mini_batches,const char* db_type
		, char** argv);




int main(int argc, char** argv) {

	if(argc<3){
		LOG(ERROR)<<
		"config storepath";
		return 1;
	}
	std::ifstream fin(argv[1]);


	std::cout<<"using CPU \n";
	std::string model_name,net_name;
	int feature_num;
	std::vector<std::string> blob_names;
	std::vector<std::string> dataset_names;
	char str[10];


	int num_mini_batches;

	fin>>model_name;
	fin>>net_name;
	std::cout<<model_name<<" "<<net_name<<std::endl;
	fin>>feature_num;
	blob_names.resize(feature_num);
	dataset_names.resize(feature_num);
	for(int i=0;i<feature_num;i++){
		fin>>blob_names[i];
		fin>>dataset_names[i];
	}
	fin>>num_mini_batches;
	fin>>str;

	fin.close();

	return feature_extraction_pipeline<float>(model_name,net_name,blob_names,dataset_names,num_mini_batches,str,argv);

}


template<typename Dtype>
int feature_extraction_pipeline(std::string pretrained_binary_proto,std::string feature_extraction_proto,
		std::vector<std::string> blob_names,std::vector<std::string> dataset_names,
		int num_mini_batches,const char* db_type
		, char** argv) {
	::google::InitGoogleLogging(argv[0]);

	int len=strlen(argv[2]);
	std::string store_path(argv[2],len);

	std::ofstream fout;
  //const int num_required_args = 7;
  /*
  if (argc < num_required_args) {
    LOG(ERROR)<<
    "This program takes in a trained network and an input data layer, and then"
    " extract features of the input data produced by the net.\n"
    "Usage: extract_features  pretrained_net_param"
    "  feature_extraction_proto_file  extract_feature_blob_name1[,name2,...]"
    "  save_feature_dataset_name1[,name2,...]  num_mini_batches  db_type"
    "  [CPU/GPU] [DEVICE_ID=0]\n"
    "Note: you can extract multiple features in one pass by specifying"
    " multiple feature blob names and dataset names seperated by ','."
    " The names cannot contain white space characters and the number of blobs"
    " and datasets must be equal.";
    return 1;
  }*/
  //int arg_pos = num_required_args;

  //arg_pos = num_required_args;
  /*if (argc > arg_pos && strcmp(argv[arg_pos], "GPU") == 0) {
    LOG(ERROR)<< "Using GPU";
    uint device_id = 0;
    if (argc > arg_pos + 1) {
      device_id = atoi(argv[arg_pos + 1]);
      CHECK_GE(device_id, 0);
    }
    LOG(ERROR) << "Using Device_id=" << device_id;
    Caffe::SetDevice(device_id);
    Caffe::set_mode(Caffe::GPU);
  } else {
    LOG(ERROR) << "Using CPU sdfsdfa";

  }
  */
	Caffe::set_mode(Caffe::CPU);


  // Expected prototxt contains at least one data layer such as
  //  the layer data_layer_name and one feature blob such as the
  //  fc7 top blob to extract features.
  /*
   layers {
     name: "data_layer_name"
     type: DATA
     data_param {
       source: "/path/to/your/images/to/extract/feature/images_leveldb"
       mean_file: "/path/to/your/image_mean.binaryproto"
       batch_size: 128
       crop_size: 227
       mirror: false
     }
     top: "data_blob_name"
     top: "label_blob_name"
   }
   layers {
     name: "drop7"
     type: DROPOUT
     dropout_param {
       dropout_ratio: 0.5
     }
     bottom: "fc7"
     top: "fc7"
   }
   */
  shared_ptr<Net<Dtype> > feature_extraction_net(
      new Net<Dtype>(feature_extraction_proto, caffe::TEST));
  feature_extraction_net->CopyTrainedLayersFrom(pretrained_binary_proto);

  size_t num_features = blob_names.size();

  for (size_t i = 0; i < num_features; i++) {
    CHECK(feature_extraction_net->has_blob(blob_names[i]))
        << "Unknown feature blob name " << blob_names[i]
        << " in the network " << feature_extraction_proto;
  }


  std::vector<shared_ptr<db::DB> > feature_dbs;
  std::vector<shared_ptr<db::Transaction> > txns;
  for (size_t i = 0; i < num_features; ++i) {
    LOG(INFO)<< "Opening dataset " << dataset_names[i];
    shared_ptr<db::DB> db(db::GetDB(db_type));
    db->Open(dataset_names.at(i), db::NEW);
    feature_dbs.push_back(db);
    shared_ptr<db::Transaction> txn(db->NewTransaction());
    txns.push_back(txn);
  }

  LOG(ERROR)<< "Extacting Features";

  Datum datum;
  const int kMaxKeyStrLength = 100;
  char key_str[kMaxKeyStrLength];
  std::vector<Blob<float>*> input_vec;
  std::vector<int> image_indices(num_features, 0);
  for (int batch_index = 0; batch_index < num_mini_batches; ++batch_index) {
    feature_extraction_net->Forward(input_vec);

    for (int i = 0; i < num_features; ++i) {
      fout.open((store_path+blob_names[i]+".out").c_str(),std::fstream::app);
      const shared_ptr<Blob<Dtype> > feature_blob = feature_extraction_net
          ->blob_by_name(blob_names[i]);

      int batch_size = feature_blob->num();
      int dim_features = feature_blob->count() / batch_size;


      const Dtype* feature_blob_data;
      for (int n = 0; n < batch_size; ++n) {
        datum.set_height(feature_blob->height());
        datum.set_width(feature_blob->width());
        datum.set_channels(feature_blob->channels());
        datum.clear_data();
        datum.clear_float_data();
        feature_blob_data = feature_blob->cpu_data() +
            feature_blob->offset(n);
        int bh=feature_blob->height();
        int bw=feature_blob->width();
        int bc=feature_blob->channels();
        snprintf(key_str, kMaxKeyStrLength, "%010d",image_indices[i]);
        float temp=0;
        fout<<"["<<key_str<<" :] ";
        for(int c=0;c<bc;c++){
        	for(int h=0;h<bh;h++){
        		for(int w=0;w<bw;w++){
        			temp+=feature_blob_data[(((c*bh)+h)*bw)+w];
        			snprintf(key_str,kMaxKeyStrLength,"%2.5f",feature_blob_data[(((c*bh)+h)*bw)+w]);
        			fout<<key_str<<" , ";
        		}//fout<<'\n';
        	}//fout<<'\n';
        }fout<<'\n';
        fout<<temp<<std::endl;
        int length = snprintf(key_str, kMaxKeyStrLength, "%010d",image_indices[i]);

        for (int d = 0; d < dim_features; ++d) {
          datum.add_float_data(feature_blob_data[d]);
          //fout<<feature_blob_data[d]<<" , ";
          //for(int )
        }//fout<<std::endl;


        string out;
        CHECK(datum.SerializeToString(&out));
        txns.at(i)->Put(std::string(key_str, length), out);
        ++image_indices[i];
        if (image_indices[i] % 1000 == 0) {
          txns.at(i)->Commit();
          txns.at(i).reset(feature_dbs.at(i)->NewTransaction());
          LOG(ERROR)<< "Extracted features of " << image_indices[i] <<
              " query images for feature blob " << blob_names[i];
        }
      }  // for (int n = 0; n < batch_size; ++n)
      fout.close();
    }  // for (int i = 0; i < num_features; ++i)
  }  // for (int batch_index = 0; batch_index < num_mini_batches; ++batch_index)
  // write the last batch
  for (int i = 0; i < num_features; ++i) {
    if (image_indices[i] % 1000 != 0) {
      txns.at(i)->Commit();
    }
    LOG(ERROR)<< "Extracted features of " << image_indices[i] <<
        " query images for feature blob " << blob_names[i];
    feature_dbs.at(i)->Close();
  }

  LOG(ERROR)<< "Successfully extracted the features!";
  //fout.close();
  return 0;
}

