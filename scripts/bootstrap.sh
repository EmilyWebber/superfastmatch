mkdir external
curl http://google-ctemplate.googlecode.com/files/ctemplate-1.0.tar.gz -o external/ctemplate-1.0.tar.gz
curl http://google-perftools.googlecode.com/files/google-perftools-1.8.3.tar.gz -o external/google-perftools-1.8.3.tar.gz
curl http://google-gflags.googlecode.com/files/gflags-1.6.tar.gz -o external/gflags-1.6.tar.gz
curl http://fallabs.com/kyotocabinet/pkg/kyotocabinet-1.2.70.tar.gz -o external/kyotocabinet-1.2.70.tar.gz
curl http://fallabs.com/kyototycoon/pkg/kyototycoon-0.9.51.tar.gz -o external/kyototycoon-0.9.51.tar.gz
svn checkout http://google-sparsehash.googlecode.com/svn/trunk/ external/google-sparsehash

cd external && for i in *.tar.gz; do tar xzvf $i; done
cd ctemplate* && ./configure && make && sudo make install && cd ..
cd google-perftools* && ./configure && make && sudo make install  && cd ..
cd gflags* && ./configure && make && sudo make install  && cd ..
cd kyotocabinet* && ./configure && make && sudo make install && cd ..
cd kyototycoon* && ./configure && make && sudo make install && cd ..
cd google-sparsehash* && ./configure && make && sudo make install && cd ..
cd ..
