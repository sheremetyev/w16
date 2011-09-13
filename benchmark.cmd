@echo off
rem compare to http://v8.googlecode.com/svn/data/benchmarks/v6/run.html
SET W16=%~dp0w16\Debug\w16.exe
pushd benchmarks\
%W16% run.js %*
popd
