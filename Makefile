CXX=g++-6
CXXFLAGS=-O3 -std=c++11
TARGETS=git-recent
INSTALL_DIR=~/bin/

git-recent: git-recent.o
	$(CXX) -o $@ $+ -lgit2

install:
	install -b -D -m 755 git-recent $(INSTALL_DIR)

clean:
	rm -f *.o $(TARGETS)
