# coding: utf-8
from __future__ import print_function
import httplib2
import os
from pykakasi import kakasi
import mojimoji

from apiclient import discovery
from oauth2client import client
from oauth2client import tools
from oauth2client.file import Storage

import datetime

try:
    import argparse
    flags = argparse.ArgumentParser(parents=[tools.argparser]).parse_args()
except ImportError:
    flags = None

SCOPES ='https://www.googleapis.com/auth/calendar.readonly'
CLIENT_SCOPE_FILE = 'client_secret.json'
APPLICATION_NAME = 'Study# google API test'

def get_credentials():
    home_dir = os.path.expanduser('~')
    credential_dir = os.path.join(home_dir, '.credentials')
    if not os.path.exists(credential_dir):
        os.makedirs(credential_dir)
    credential_path = os.path.join(credential_dir, 'calendar-python-quickstart.json')

    store = Storage(credential_path)
    credentials = store.get()
    if not credentials or credentials.invalid:
        flow = client.flow_from_clientsecrets(CLIENT_SCOPE_FILE, SCOPES)
        flow.user_agent=APPLICATION_NAME
        if flags:
            credentials = tools.run_flow(flow,store,flags)
        else:
            credentials = tools.run(flow,store)
        print('Storing credentials to '+credential_path)
    return credentials

def main():
    credentials = get_credentials()
    http = credentials.authorize(httplib2.Http())
    service=discovery.build('calendar','v3',http=http)

    now = datetime.datetime.utcnow().isoformat() + 'Z'
    print('Getting the upcoming 40 events')
    eventsResult = service.events().list(
        calendarId='primary',timeMin=now,maxResults=40,singleEvents=True,
        orderBy='startTime').execute()
    events = eventsResult.get('items',[])

    if not events:
        print('No upcoming events found.')

    addNewEvent(events)

def string2datetime(str_time):
    int_year = int(str_time[0:4])
    int_month= int(str_time[5:7])
    int_day  = int(str_time[8:10])
    int_hour = int(str_time[11:13])
    int_min  = int(str_time[14:16])
    int_sec  = int(str_time[17:19])
    dt_time  = datetime.datetime(int_year,int_month,int_day,int_hour,int_min,int_sec,int_sec)
    return dt_time

def deleteOldEvent():
    return

def addNewEvent(events):
    path = 'data/src/'
    with open((path+"report.txt")) as fr:
        lr=fr.readlines()
    with open((path+"test.txt")) as ft:
        lt=ft.readlines()
    with open((path+"goods.txt")) as fg:
        lg=fg.readlines()
    with open((path+"event.txt")) as fe:
        le=fe.readlines()
    with open((path+"allevent.txt")) as fa:
        la=fa.readlines()

    fr=open((path+'report.txt'),mode='w')
    ft=open((path+'test.txt'),mode='w')
    fg=open((path+'goods.txt'),mode='w')
    fe=open((path+'event.txt'),mode='w')
    fa=open((path+'allevent.txt'),mode='w')

    for event in events:
        start = event['start'].get('dateTime',event['start'].get('date'))
        eventName = event['summary'].encode('utf-8')
        dates = string2datetime(start)
        eventTime = "{:0<4}{:0<2}{:0<2}{:0<2}".format(dates.year,dates.month,dates.day,dates.hour)

        if(eventName[0:2]=='SS'):
            flag = True
            for word in la:
                if(eventTime==word[0:10] and eventName[6:]==word[10:-1]):
                    flag = False
                    break
            if(not flag):
                continue


            kks = kakasi()

            # kks.setMode('H','K')
            # kks.setMode('K','K')
            kks.setMode("J","K")

            conv = kks.getConverter()
            onlyKana = conv.do(eventName[6:].decode('utf-8'))
            onlyKana = mojimoji.zen_to_han(onlyKana)

            la.insert(0,eventTime+eventName[6:]+'\n') #write all events
            if(eventName[2]=='R'):
                # print(start, "report" , eventName[6:])
                print(start, "report" ,onlyKana)
                lr.insert(0,eventName[6:]+'\n')
            if(eventName[3]=='T'):
                # print(start, "test  " , eventName[6:])
                print(start, "test  " ,onlyKana)
                lt.insert(0,eventName[6:]+'\n')
            if(eventName[4]=='G'):
                # print(start, "goods " , eventName[6:])
                print(start, "goods " ,onlyKana)
                lg.insert(0,eventName[6:]+'\n')
            if(eventName[5]=='E'):
                # print(start, "event " , eventName[6:])
                print(start, "event " ,onlyKana)
                le.insert(0,eventName[6:]+'\n')

    fr.writelines(lr)
    ft.writelines(lt)
    fg.writelines(lg)
    fe.writelines(le)
    fa.writelines(la)
    fr.close()
    ft.close()
    fg.close()
    fe.close()
    fa.close()

def updateEvent():
    return

if __name__ =='__main__':
    #make files
    path = 'data/src/'
    try:
        with open((path+'report.txt'),mode='r'):
            pass
        with open((path+'test.txt'),mode='r'):
            pass
        with open((path+'goods.txt'),mode='r'):
            pass
        with open((path+'event.txt'),mode='r'):
            pass
        with open((path+'allevent.txt'),mode='r'):
            pass
    except IOError as e:
        with open((path+'report.txt'),mode='w'):
            pass
        with open((path+'test.txt'),mode='w'):
            pass
        with open((path+'goods.txt'),mode='w'):
            pass
        with open((path+'event.txt'),mode='w'):
            pass
        with open((path+'allevent.txt'),mode='w'):
            pass

    main()
