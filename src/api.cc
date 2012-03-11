#include "api.h"

namespace superfastmatch{
  
	RegisterTemplateFilename(SUCCESS_JSON, "JSON/success.tpl");
	RegisterTemplateFilename(FAILURE_JSON, "JSON/failure.tpl");
	RegisterTemplateFilename(DESCRIPTION_HTML, "HTML/description.tpl");

  // -------------------  
  // ApiResponse members
  // -------------------
  
  ApiResponse::ApiResponse():
  type(-1,"text/html"),
  dict("response")
  {}
    
  // -------------------  
  // ApiParams members
  // -------------------
    
  ApiParams::ApiParams(const HTTPClient::Method verb,const string& body, const map<string,string>& misc)
  {
    if (verb==HTTPClient::MPOST || verb==HTTPClient::MPUT){
      wwwformtomap(body,&form);
    }
    map<string,string>::const_iterator it=misc.find("query");
    if (it!=misc.end()){
      vector<string> queries,parts;
      kc::strsplit(it->second,"&",&queries);
      for (vector<string>::const_iterator it2=queries.begin();it2!=queries.end();it2++){
        kc::strsplit(*it2,"=",&parts);
        if (parts.size()==2){
          query[parts[0]]=query[parts[1]];
        }
      }  
    }
  }

  // -------------------
  // ApiCall members
  // -------------------
  
  ApiCall::ApiCall(const HTTPClient::Method verb,
                   const char* match,
                   const response_map& responses,
                   const char* description,
                   const ApiMethod method):
    verb(verb),
    match(match),
    responses(responses),
    description(description),
    method(method){}
    
  // -------------------
  // Api members
  // -------------------
    
  const size_t Api::METHOD_COUNT=6;
  
  Api::Api(Registry* registry):
    registry_(registry)
  {
    for (size_t i=0;i<Api::METHOD_COUNT;i++){
      matchers_.push_back(MatcherPtr(new Matcher()));
    }
    for (size_t i=0;i<Api::API_COUNT;i++){
      MatcherPtr matcher=matchers_[calls_[i].verb];
      int atom_id;
      assert(matcher->f.Add(calls_[i].match,matcher->options,&atom_id)==re2::RE2::NoError);
      matcher->atom_indices.push_back(atom_id);
      matcher->regexes[atom_id]=RE2Ptr(new re2::RE2(calls_[i].match));
      matcher->calls[atom_id]=&calls_[i];
    }
    for(size_t i=0;i<METHOD_COUNT;i++){
      if (matchers_[i]->f.NumRegexps()>0){
        matchers_[i]->f.Compile(&matchers_[i]->atoms); 
      }
    }
  }

  int Api::Invoke(const string& path, HTTPClient::Method verb,
                   const map<string, string>& reqheads,
                   const string& reqbody,
                   map<string, string>& resheads,
                   string& resbody,
                   const map<string, string>& misc)
  {
    ApiParams params(verb,reqbody,misc);
    params.url=misc.find("url")->second;    //TODO: Remove this
    ApiResponse response;
    string lowercase_path(path);
    kc::strtolower(&lowercase_path);
    MatcherPtr matcher=matchers_[verb];
    int id=MatchApiCall(matcher,lowercase_path,params);
    if(id!=-1){
      (this->*matcher->calls[id]->method)(params,response);
      map<response_t,string>::const_iterator it=matcher->calls[id]->responses.find(response.type);
      if (it!=matcher->calls[id]->responses.end()){
        registry_->getTemplateCache()->ExpandWithData(it->second,STRIP_BLANK_LINES,&response.dict,NULL,&resbody); 
      }
      resheads["content-type"] = response.type.second;
    }
    return response.type.first;
  } 
  
  int Api::MatchApiCall(MatcherPtr matcher,const string& path,ApiParams& params){
    int id=matcher->f.FirstMatch(path,matcher->atom_indices);
    if (id!=-1){
      RE2Ptr regex=matcher->regexes[id];
      string* captures=new string[regex->NumberOfCapturingGroups()];
      RE2::Arg* argv=new RE2::Arg[regex->NumberOfCapturingGroups()];
      RE2::Arg** args=new RE2::Arg*[regex->NumberOfCapturingGroups()];
      for(int i=0;i<regex->NumberOfCapturingGroups();i++){
        argv[i]=&captures[i];
        args[i]=&argv[i];
      };
      if (re2::RE2::FullMatchN(path,*regex,args,regex->NumberOfCapturingGroups())){
        for (map<string,int>::const_iterator it=regex->NamedCapturingGroups().begin(),ite=regex->NamedCapturingGroups().end();it!=ite;++it){
          params.resource[it->first]=captures[it->second-1];
        }
      }
      delete[] args;
      delete[] argv;
      delete[] captures;
    }
    return id;
  }

  // --------------------
  // Api call definitions
  // --------------------

  const size_t Api::API_COUNT=8;

  const ApiCall Api::calls_[Api::API_COUNT]={
    ApiCall(HTTPClient::MPOST,
            "^search/?$",
            create_map<response_t,string>(response_t(200,"application/json"),SUCCESS_JSON)\
                                         (response_t(500,"application/json"),SUCCESS_JSON),  
            "Search for text in all documents",
            &Api::DoSearch),
    ApiCall(HTTPClient::MGET,
            "^document/?$",
            create_map<response_t,string>(response_t(200,"application/json"),SUCCESS_JSON)\
                                         (response_t(500,"application/json"),FAILURE_JSON),
            "Get metadata and text of all documents",
            &Api::GetDocuments),
    ApiCall(HTTPClient::MGET,
            "^document/(?P<doctype>\\d+)/?$",
            create_map<response_t,string>(response_t(200,"application/json"),SUCCESS_JSON)\
                                         (response_t(500,"application/json"),FAILURE_JSON),
            "Get metadata and text of all documents with specified doctype",
            &Api::GetDocuments),
    ApiCall(HTTPClient::MGET,
            "^document/(?P<doctype>\\d+)/(?P<docid>\\d+)/?$",
            create_map<response_t,string>(response_t(202,"application/json"),SUCCESS_JSON)\
                                         (response_t(404,"application/json"),FAILURE_JSON),
            "Get metadata and text of existing document",
            &Api::GetDocument),
    ApiCall(HTTPClient::MPOST,
           "^document/(?P<doctype>\\d+)/(?P<docid>\\d+)/?$",
           create_map<response_t,string>(response_t(202,"application/json"),SUCCESS_JSON)\
                                        (response_t(500,"application/json"),FAILURE_JSON),
           "Create a new document",
           &Api::CreateDocument),
    ApiCall(HTTPClient::MPUT,
           "^document/(?P<doctype>\\d+)/(?P<docid>\\d+)/?$",
           create_map<response_t,string>(response_t(202,"application/json"),SUCCESS_JSON)\
                                        (response_t(500,"application/json"),FAILURE_JSON),
           "Create and associate a new document",
           &Api::CreateAndAssociateDocument),
    ApiCall(HTTPClient::MGET,
           "^status/?$",
           create_map<response_t,string>(response_t(200,"application/json"),SUCCESS_JSON),
           "Get the status of the superfastmatch instance",
           &Api::GetStatus),
    ApiCall(HTTPClient::MGET,
           "^describe/?$",
           create_map<response_t,string>(response_t(200,"text/html"),DESCRIPTION_HTML),
           "Describe the superfastmatch API",
           &Api::GetDescription)                                                 
  };

  // ------------------------
  // Api call implementations
  // ------------------------

  void Api::DoSearch(const ApiParams& params,ApiResponse& response){
    map<string,string>::const_iterator text=params.form.find("text");
    if (text!=params.form.end()){
      SearchPtr search=Search::createTemporarySearch(registry_,text->second);
      search->fillJSONDictionary(&response.dict,false);
      response.type=response_t(200,"application/json");
    }else{
      response.type=response_t(500,"application/json");
      response.dict.SetValue("MESSAGE","No text field specified");  
    }
  }

  void Api::GetDocument(const ApiParams& params,ApiResponse& response){
    uint32_t doctype = kc::atoi(params.resource.find("doctype")->second.c_str());
    uint32_t docid = kc::atoi(params.resource.find("docid")->second.c_str());
    SearchPtr search=Search::getPermanentSearch(registry_,doctype,docid);
    if (search){
      search->fillJSONDictionary(&response.dict,true);
      response.type=response_t(200,"application/json");
    }else{
      response.type=response_t(404,"application/json");        
      response.dict.SetValue("MESSAGE","Document not found.");
    }
  }
  
  void Api::GetDocuments(const ApiParams& params,ApiResponse& response){
    DocumentQuery query(registry_,params.url); //TODO change query constructor
    if (query.isValid()){
      query.fillJSONDictionary(&response.dict);
      response.type=response_t(200,"application/json");
    }else{
      response.type=response_t(404,"application/json");        
      response.dict.SetValue("MESSAGE","Invalid query.");
    }
  }
  
  void Api::CreateDocument(const ApiParams& params,ApiResponse& response){
    uint32_t doctype = kc::atoi(params.resource.find("doctype")->second.c_str());
    uint32_t docid = kc::atoi(params.resource.find("docid")->second.c_str());
    map<string,string>::const_iterator text=params.form.find("text");
    if (text!=params.form.end()){
      CommandPtr addCommand = registry_->getQueueManager()->createCommand(AddDocument,doctype,docid,text->second);
      addCommand->fillDictionary(&response.dict);
      response.type=response_t(202,"application/json");        
    }else{
      response.type=response_t(500,"application/json");   
      response.dict.SetValue("MESSAGE","No text field specified");     
    }
  }

  void Api::CreateAndAssociateDocument(const ApiParams& params,ApiResponse& response){
    CreateDocument(params,response);
    if (response.type.first==202){
      uint32_t doctype = kc::atoi(params.resource.find("doctype")->second.c_str());
      uint32_t docid = kc::atoi(params.resource.find("docid")->second.c_str());
      CommandPtr associateCommand = registry_->getQueueManager()->createCommand(AddAssociation,doctype,docid,"");
      associateCommand->fillDictionary(&response.dict);
    }
  }
  
  void Api::GetStatus(const ApiParams& params,ApiResponse& response){
    registry_->fillStatusDictionary(&response.dict);
    registry_->getPostings()->fillStatusDictionary(&response.dict);
    response.type=response_t(200,"application/json");
  }
  
  void Api::GetDescription(const ApiParams& params,ApiResponse& response){
    response.type=response_t(200,"application/json");
  }
}