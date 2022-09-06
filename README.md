# nethaspmon
Nethaspmon is utility for monitoring 1C user licenses with zabbix.
</br>
Nethaspmon run as windows service. 
</br>
Nethaspmon uses hsmon.dll from 1C Aladdin HASP.
</br>
Config nethasp.ini in format for Aladdin HASP.
</br>
Install windows service: nethaspmon.exe -c path_to_conf -i
</br>
Uninstall windows service: nethaspmon.exe -u
</br>
Nethaspmon every 5 minutes scan license servers when it is running.
</br>
Scanning result available by http://localhost:27015
</br>
