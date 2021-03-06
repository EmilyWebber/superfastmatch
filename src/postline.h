#ifndef _SFMPOSTLINE_H                       // duplication check
#define _SFMPOSTLINE_H

#include <common.h>
#include <codec.h>

using namespace std;

namespace superfastmatch
{
  class PostLine{
  private:
    PostLineCodec* codec_;
    unsigned char* start_;
    unsigned char* temp_header_;
    unsigned char* temp_sections_;
    size_t old_header_length_;
    size_t temp_header_length_;
    size_t temp_sections_length_;
    uint32_t updated_section_;
    vector<uint32_t> section_;
    vector<PostLineHeader>* header_;
    vector<uint32_t>* deltas_;
    vector<uint32_t>* docids_;
  
  public:
    PostLine(uint32_t max_length);
    ~PostLine();

    vector<PostLineHeader>* load(const unsigned char* start);
    
    // Returns false if there are no changes
    // out must have a length greater than or equal to getLength()
    bool commit(unsigned char* out);
    
    // Returns false if the doc_type or doc_id are 0
    // commit must be called after each add operation
    bool addDocument(const uint32_t doc_type,const uint32_t doc_id);
    
    // Returns false if the doc_type or doc_id are 0 or the document is not present
    // commit must be called after each succesful delete operation
    bool deleteDocument(const uint32_t doc_type,const uint32_t doc_id);

    size_t getLength();
    size_t getLength(const uint32_t doc_type);
    vector<uint32_t>* getDocIds(const uint32_t doc_type);
    vector<uint32_t>* getDeltas(const uint32_t doc_type);
    
    friend std::ostream& operator<< (std::ostream& stream, PostLine& postline);
  private:
    DISALLOW_COPY_AND_ASSIGN(PostLine);
  };
}
#endif
