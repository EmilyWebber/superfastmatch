#include "posting.h"

//Is this naughty?
#include "document.h"
// #include "command.h"
#include "registry.h"
#include "association.h"

namespace superfastmatch
{
  RegisterTemplateFilename(HISTOGRAM, "histogram.tpl");
  
  // -------------------
  // PostingSlot members
  // -------------------
  
  PostingSlot::PostingSlot(Registry* registry,uint32_t slot_number):
  registry_(registry),slot_number_(slot_number),
  offset_((registry->getMaxHashCount()/registry->getSlotCount())*slot_number), // Offset for sparsetable insertion
  span_(registry->getMaxHashCount()/registry->getSlotCount()), // Span of slot, ie. ignore every hash where (hash-offset)>span
  index_(registry->getMaxHashCount()/registry->getSlotCount())
  {
    // 1 thread per slot, increase slot_number to get more threads!
    queue_.start(1);
  }
  
  PostingSlot::~PostingSlot(){
    queue_.finish();
    // This doesn't clear the pointers but we don't care because we are quitting
    index_.clear();
  }
  
  void PostingSlot::finishTasks(){
    queue_.finish();
    queue_.start(1);
  }
  
  size_t PostingSlot::getHashCount(){
    size_t hash_count;
    index_lock_.lock_reader();
    hash_count=index_.num_nonempty();
    index_lock_.unlock();
    return hash_count;
  }
  
  void PostingSlot::lockSlotForReading(){
    index_lock_.lock_reader(); 
  }
  
  void PostingSlot::unlockSlotForReading(){
    index_lock_.unlock();
  }

  bool PostingSlot::alterIndex(DocumentPtr doc,TaskPayload::TaskOperation operation){
    PostLine line(registry_->getMaxLineLength());
    uint32_t hash;
    uint32_t hash_mask=registry_->getHashMask();
    uint32_t white_space=registry_->getWhiteSpaceHash()-offset_;
    bool white_space_seen=false;
    uint32_t hash_width=registry_->getHashWidth();
    size_t incoming_length;
    size_t outgoing_length;
    // Where hash width is below 32 we will get duplicates per document
    // We discard them with a no operation 
    for (hashes_vector::const_iterator it=doc->getPostingHashes().begin(),ite=doc->getPostingHashes().end();it!=ite;++it){
      hash = ((*it>>hash_width)^(*it&hash_mask))-offset_;
      uint32_t doctype=doc->doctype();
      uint32_t docid=doc->docid();
      if (hash<span_){
        if (hash==white_space){
          if (white_space_seen){
            //Shortcut!
            continue;
          }
          white_space_seen=true;
        }
        unsigned char* entry=NULL;
        bool noop=true;
        index_lock_.lock_writer();
        if (!index_.test(hash)){
          entry = new unsigned char[8];
          memset(entry,0,8);
          index_.set(hash,entry);
        }
        entry=index_.unsafe_get(hash);
        line.load(entry);
        incoming_length=line.getLength();
        if((incoming_length+5)<=(registry_->getMaxLineLength())){
          switch (operation){
            case TaskPayload::AddDocument:
              noop=not line.addDocument(doctype,docid);
              break;
            case TaskPayload::DeleteDocument:
              noop=not line.deleteDocument(doctype,docid);
              break;
            }
        }
        if (!noop){
          outgoing_length=line.getLength();
          if ((outgoing_length/8)>(incoming_length/8)){
            size_t size=((outgoing_length/8)+1)*8;
            delete[] entry;
            entry = new unsigned char[size];
            index_.set(hash,entry);
          }
          line.commit(entry);
        }
        index_lock_.unlock();
      }
    }
    return true;
  }

  bool PostingSlot::searchIndex(const uint32_t doctype,const uint32_t docid,const uint32_t hash, const uint32_t position,PostLine& line,search_t& results){
    const uint32_t slot_hash = hash-offset_;
    const uint8_t current=position&0xFF;
    if(index_.test(slot_hash)){
      const unsigned char* start=index_.unsafe_get(slot_hash);
      vector<PostLineHeader>* doctypes=line.load(start);
      for (vector<PostLineHeader>::const_iterator it=doctypes->begin(),ite=doctypes->end();it!=ite;++it){
        vector<uint32_t>* docids=line.getDocIds(it->doc_type);
        for (vector<uint32_t>::const_iterator it2=docids->begin(),ite2=docids->end();it2!=ite2;++it2){
          DocPair pair(it->doc_type,*it2);
          DocTally* tally=&results[pair];
          const bool notSearchDoc=(doctype-it->doc_type)|(docid-*it2);
          uint64_t barrel=tally->previous;
          const uint8_t previous=(barrel)&0xFF;
          const uint8_t difference=current-previous;
          barrel=((barrel&0xFFFFFFFFFFFFFF00)<<8)|(uint64_t(difference)<<8)|current;
          tally->previous=barrel;
          const bool notConsecutive=(barrel&0xFFFFFFFFFFFFFF00)-0x0101010101010100;
          const bool isMatch=notSearchDoc&(!notConsecutive);
          tally->count+=isMatch;
        }
      }
    }
    return true;
  }
  
  uint64_t PostingSlot::addTask(TaskPayload* payload){
    PostingTask* task = new PostingTask(this,payload);
    uint64_t queue_length=queue_.add_task(task);
    if (queue_length>40){
      for (double wsec = 0.1; true; wsec *= 2) {
        if (wsec > 1.0) wsec = 1.0;
        kc::Thread::sleep(wsec);
        queue_length=queue_.count();
        if (queue_length<=20) break;
      }
    }
    return queue_length;
  }
  
  uint64_t PostingSlot::getTaskCount(){
    return queue_.count();
  }
  
  uint32_t PostingSlot::fillListDictionary(TemplateDictionary* dict,uint32_t start){
    PostLine line(registry_->getMaxLineLength());
    index_lock_.lock_reader();
    uint32_t count=0;
    uint32_t hash=(offset_>start)?0:start-offset_;
    // Find first hash
    while (hash<span_ && !index_.test(hash)){
      hash++;
    }
    // If not in slot break early
    if (index_.num_nonempty()==0 || hash>=span_){
        index_lock_.unlock();
        return 0;
    }
    // Scan non empty
    index_t::nonempty_iterator it=index_.get_iter(hash);
    while (it!=index_.nonempty_end() && count<registry_->getPageSize()){
      count++;
      vector<PostLineHeader>* doc_types=line.load(*it);
      TemplateDictionary* posting_dict=dict->AddSectionDictionary("POSTING");
      TemplateDictionary* hash_dict=posting_dict->AddSectionDictionary("HASH");
      hash_dict->SetIntValue("HASH",index_.get_pos(it)+offset_);
      hash_dict->SetIntValue("BYTES",line.getLength());
      hash_dict->SetIntValue("DOC_TYPE_COUNT",doc_types->size());
      for (size_t i=0;i<doc_types->size();i++){
        posting_dict->SetIntValue("DOC_TYPE",(*doc_types)[i].doc_type);
        vector<uint32_t>* doc_ids=line.getDocIds((*doc_types)[i].doc_type);
        uint32_t previous=0;
        for (size_t j=0;j<doc_ids->size();j++){
          TemplateDictionary* doc_id_dict = posting_dict->AddSectionDictionary("DOC_IDS");
          TemplateDictionary* doc_delta_dict = posting_dict->AddSectionDictionary("DOC_DELTAS");
          doc_id_dict->SetIntValue("DOC_ID",(*doc_ids)[j]); 
          doc_delta_dict->SetIntValue("DOC_DELTA",(*doc_ids)[j]-previous);
          previous=(*doc_ids)[j];
        }
        if (i!=(doc_types->size()-1)){
          posting_dict=dict->AddSectionDictionary("POSTING");
        }
      }
      it++;
    }
    index_lock_.unlock();
    return count;
  }
  
  void PostingSlot::fillHistograms(histogram_t& hash_hist,histogram_t& gaps_hist){
    PostLine line(registry_->getMaxLineLength());
    index_lock_.lock_reader();
    for (index_t::nonempty_iterator it=index_.nonempty_begin(),ite=index_.nonempty_end();it!=ite;++it){
      vector<PostLineHeader>* doc_types=line.load(*it);
      for(vector<PostLineHeader>::const_iterator it=doc_types->begin(),ite=doc_types->end();it!=ite;++it){
        const uint32_t doc_type=it->doc_type;
        vector<uint32_t>* doc_deltas=line.getDeltas(doc_type);
        hash_hist[doc_type][doc_deltas->size()]++;
        stats_t* gaps=&gaps_hist[doc_type];
        for (vector<uint32_t>::const_iterator it2=doc_deltas->begin(),ite2=doc_deltas->end();it2!=ite2;++it2){
          (*gaps)[*it2]++;
        }
      }
    }
    index_lock_.unlock();
  }
  
  //-------------------------------
  // Posting Instrument definitions
  //-------------------------------
  
  enum PostingTimers{
    SEARCH,
    INIT
  };

  enum AssociationCounters{
    DOC_TYPE,
    DOC_ID,
    WINDOW_SIZE,
    DOC_LENGTH,
    RESULT_COUNT,
    COLLISIONS,
    BUCKET_COUNT,
    RESULTS_ESTIMATE,
    WHITESPACE_COUNT
  };
    
  template<> const InstrumentDefinition Instrumented<Posting>::getDefinition(){
    return InstrumentDefinition("Posting",SEARCH,create_map<int32_t,string>(SEARCH,"Search")(INIT,"Initialisation"),
                                create_map<int32_t,string>(DOC_TYPE,"Doc Type")
                                                          (DOC_ID,"Doc Id")
                                                          (WINDOW_SIZE,"Window Size")
                                                          (DOC_LENGTH,"Document Length")
                                                          (COLLISIONS,"Collisions")
                                                          (RESULT_COUNT,"Result Count")
                                                          (BUCKET_COUNT,"Bucket Count")
                                                          (RESULTS_ESTIMATE,"Results Estimate")
                                                          (WHITESPACE_COUNT,"Whitespace Count"));
  }
  
  // ---------------
  // Posting members
  // ---------------
  
  Posting::Posting(Registry* registry):
  registry_(registry),
  doc_count_(0),
  total_doc_length_(0),
  ready_(false)
  {
    for (uint32_t i=0;i<registry->getSlotCount();i++){
      slots_.push_back(new PostingSlot(registry,i));
    }
  }
  
  Posting::~Posting(){
    for (size_t i=0;i<slots_.size();i++){
      delete slots_[i];
    }
  }
  
  bool Posting::init(){
    // Load the stored docs
    double start = kc::time();
    DocumentQuery query(registry_);
    assert(query.isValid());
    vector<DocPair> pairs=query.getSourceDocPairs(true);
    for(vector<DocPair>::iterator it=pairs.begin(),ite=pairs.end();it!=ite;++it){
      DocumentPtr doc=registry_->getDocumentManager()->getDocument(it->doc_type,it->doc_id,DocumentManager::TEXT|DocumentManager::POSTING_HASHES);
      addDocument(doc);
      if(registry_->isClosing()){
        break;
      }
    }
    finishTasks();
    stringstream message;
    message << "Posting initialisation finished in: " << setiosflags(ios::fixed) << setprecision(4) << kc::time()-start << " secs";
    registry_->getLogger()->log(Logger::DEBUG,&message);
    ready_=true;
    return ready_;
  }
  
  void Posting::finishTasks(){
    for (size_t i=0;i<slots_.size();i++){
      slots_[i]->finishTasks();
    }
  }
  
  size_t Posting::getHashCount(){
    size_t hash_count=0;
    for (size_t i=0;i<slots_.size();i++){
      hash_count+=slots_[i]->getHashCount();
    }
    return hash_count;
  }
  
  void Posting::lockSlotsForReading(){
    for (size_t i=0;i<slots_.size();i++){
      slots_[i]->lockSlotForReading();
    }
  }
  
  void Posting::unlockSlotsForReading(){
    for (size_t i=0;i<slots_.size();i++){
      slots_[i]->unlockSlotForReading();
    }
  }
  
  uint64_t Posting::alterIndex(DocumentPtr doc,TaskPayload::TaskOperation operation){
    Logger* logger=registry_->getLogger();
    stringstream message;
    if (operation==TaskPayload::AddDocument){
      message << "Adding "; 
    }else{
      message << "Deleting ";
    }
    message << *doc << " Slots: ";
    uint64_t total_length=0;
    TaskPayload* task = new TaskPayload(doc,operation,slots_.size());
    for (size_t i=0;i<slots_.size();i++){
      uint64_t queue_length=slots_[i]->addTask(task);
      message << i << ":" << queue_length << " ";
      total_length+=queue_length;
    }
    message << " Total: " << total_length;
    logger->log(Logger::DEBUG,&message);
    return total_length;
  }
  
  void Posting::searchIndex(Search& search){
    InstrumentPtr perf=createInstrument();
    search.performance->add(perf);
    perf->startTimer(SEARCH);
    PostLine line(registry_->getMaxLineLength());
    const uint32_t hash_mask=registry_->getHashMask();
    const uint32_t hash_width=registry_->getHashWidth();
    const uint32_t white_space=registry_->getWhiteSpaceHash();
    const uint32_t span=registry_->getMaxHashCount()/registry_->getSlotCount();
    const uint32_t doctype=search.doc->doctype();
    const uint32_t docid=search.doc->docid();
    const uint64_t results_estimate=min(doc_count_,((total_doc_length_/registry_->getMaxHashCount())+1)*search.doc->getText().size())*3;
    perf->setCounter(DOC_TYPE,doctype);
    perf->setCounter(DOC_ID,docid);
    perf->setCounter(DOC_LENGTH, search.doc->getText().size());
    perf->setCounter(RESULTS_ESTIMATE,results_estimate);
    perf->setCounter(WINDOW_SIZE,registry_->getWindowSize());
    uint32_t position=0;
    search.results.rehash(results_estimate);
    lockSlotsForReading();
    for (vector<uint32_t>::const_iterator it=search.doc->getPostingHashes().begin(),ite=search.doc->getPostingHashes().end();it!=ite;++it){
      uint32_t hash=(*it>>hash_width)^(*it&hash_mask);
      if (hash!=white_space){
        slots_[hash/span]->searchIndex(doctype,docid,hash,position,line,search.results);
      }else{
        perf->incrementCounter(WHITESPACE_COUNT);
      }
      position++;
    }
    unlockSlotsForReading();
    perf->setCounter(BUCKET_COUNT,search.results.bucket_count());
    perf->setCounter(RESULT_COUNT,search.results.size());
    for (size_t i=0;i<search.results.bucket_count();i++){
      if (search.results.bucket_size(i)>1){
        perf->incrementCounter(COLLISIONS);
      }
    }
    for (search_t::iterator it=search.results.begin(),ite=search.results.end();it!=ite;it++){
      if ((it->second.count>0)){
        search.pruned_results.insert(pair<DocTally,DocPair>(it->second,it->first));
      }
    }
    perf->stopTimer(SEARCH);
  }
  
  uint64_t Posting::addDocument(DocumentPtr doc){
    uint64_t queue_length=alterIndex(doc,TaskPayload::AddDocument);
    doc_count_++;
    total_doc_length_+=doc->getText().size();
    return queue_length;
  }
  
  uint64_t Posting::deleteDocument(DocumentPtr doc){
    uint64_t queue_length=alterIndex(doc,TaskPayload::DeleteDocument);
    doc_count_--;
    total_doc_length_-=doc->getText().size();
    return queue_length;
  }
  
  bool Posting::isReady(){
    return ready_;
  }
  
  void Posting::fillStatusDictionary(TemplateDictionary* dict){
    size_t hash_count=0;
    for (size_t i=0;i<slots_.size();i++){
      hash_count+=slots_[i]->getHashCount();
    }
    dict->SetIntValue("WINDOW_SIZE",registry_->getWindowSize());
    dict->SetIntValue("WHITE_SPACE_THRESHOLD",registry_->getWhiteSpaceThreshold());
    dict->SetIntValue("HASH_WIDTH",registry_->getHashWidth());
    dict->SetIntValue("SLOT_COUNT",registry_->getSlotCount());
    dict->SetIntValue("DOC_COUNT",doc_count_);
    dict->SetIntValue("HASH_COUNT",hash_count);
    dict->SetIntValue("AVERAGE_HASHES",(doc_count_>0)?total_doc_length_/hash_count:0);
    dict->SetIntValue("AVERAGE_DOC_LENGTH",(doc_count_>0)?total_doc_length_/doc_count_:0);
  }
  
  void Posting::fillListDictionary(TemplateDictionary* dict,uint32_t start){
    uint32_t count=0;
    for (size_t i=0;i<slots_.size();i++){
      count+=slots_[i]->fillListDictionary(dict,start);
      if (count>=registry_->getPageSize()){
        break;
      }
    }
    TemplateDictionary* page_dict=dict->AddIncludeDictionary("PAGING");
    page_dict->SetFilename(PAGING);
    page_dict->SetValueAndShowSection("PAGE",toString(0),"FIRST");
    if (start>registry_->getPageSize()){
      page_dict->SetValueAndShowSection("PAGE",toString(start-registry_->getPageSize()),"PREVIOUS"); 
    }
    else{
      page_dict->SetValueAndShowSection("PAGE",toString(0),"PREVIOUS"); 
    }
    page_dict->SetValueAndShowSection("PAGE",toString(min(registry_->getMaxHashCount()-registry_->getPageSize(),start+registry_->getPageSize())),"NEXT");
    page_dict->SetValueAndShowSection("PAGE",toString(registry_->getMaxHashCount()-registry_->getPageSize()),"LAST");
  }

  void Posting::fillHistogramDictionary(TemplateDictionary* dict){
    histogram_t hash_hist;
    histogram_t gaps_hist;
    for (uint32_t i=0;i<slots_.size();i++){
      slots_[i]->fillHistograms(hash_hist,gaps_hist);
    }
    // Hash histogram
    TemplateDictionary* hash_dict = dict->AddIncludeDictionary("HASH_HISTOGRAM");
    hash_dict->SetFilename(HISTOGRAM);
    hash_dict->SetValue("NAME","hashes");
    hash_dict->SetValue("TITLE","Frequency of documents per hash");
    for (histogram_t::const_iterator it = hash_hist.begin(),ite=hash_hist.end();it!=ite;++it){
      hash_dict->SetValueAndShowSection("DOC_TYPE",toString(it->first),"COLUMNS");
    }
    for (uint32_t i=0;i<500;i++){
      TemplateDictionary* row_dict=hash_dict->AddSectionDictionary("ROW");
      row_dict->SetIntValue("INDEX",i);
      for (histogram_t::iterator it = hash_hist.begin(),ite=hash_hist.end();it!=ite;++it){
          row_dict->SetValueAndShowSection("DOC_COUNTS",toString(it->second[i]),"COLUMN");  
      } 
    }
    // Deltas Histogram
    TemplateDictionary* deltas_dict = dict->AddIncludeDictionary("DELTAS_HISTOGRAM");
    deltas_dict->SetFilename(HISTOGRAM);
    deltas_dict->SetValue("NAME","deltas");
    deltas_dict->SetValue("TITLE","Frequency of document deltas");
    for (histogram_t::const_iterator it = gaps_hist.begin(),ite=gaps_hist.end();it!=ite;++it){
      deltas_dict->SetValueAndShowSection("DOC_TYPE",toString(it->first),"COLUMNS");
    }
    vector<uint32_t> sorted_keys;
    for (histogram_t::iterator it = gaps_hist.begin(),ite=gaps_hist.end();it!=ite;++it){
      for (stats_t::const_iterator it2=it->second.begin(),ite2=it->second.end();it2!=ite2;++it2){
        sorted_keys.push_back(it2->first);
      }
    }
    std::sort(sorted_keys.begin(),sorted_keys.end());
    vector<uint32_t>::iterator res_it=std::unique(sorted_keys.begin(),sorted_keys.end());
    sorted_keys.resize(res_it-sorted_keys.begin());
    for (vector<uint32_t>::const_iterator it=sorted_keys.begin(),ite=sorted_keys.end();it!=ite;++it){
      TemplateDictionary* row_dict=deltas_dict->AddSectionDictionary("ROW");
      row_dict->SetIntValue("INDEX",*it);
      for (histogram_t::iterator it2 = gaps_hist.begin(),ite2=gaps_hist.end();it2!=ite2;++it2){
        row_dict->SetValueAndShowSection("DOC_COUNTS",toString(it2->second[*it]),"COLUMN"); 
      }
    } 
    fillStatusDictionary(dict);
  }
}
