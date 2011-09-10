@echo off

call "%VS100COMNTOOLS%vsvars32.bat"
devenv w16\w16.sln /Build Debug
