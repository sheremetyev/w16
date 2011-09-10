@echo off

third_party\python_26\python build/gyp_v8 -G msvs_version=2010

call "%VS100COMNTOOLS%vsvars32.bat"
devenv w16\w16.sln /Build Debug
