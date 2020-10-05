﻿/*  Omega Red - Client PS2 Emulator for PCs
*
*  Omega Red is free software: you can redistribute it and/or modify it under the terms
*  of the GNU Lesser General Public License as published by the Free Software Found-
*  ation, either version 3 of the License, or (at your option) any later version.
*
*  Omega Red is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
*  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
*  PURPOSE.  See the GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License along with Omega Red.
*  If not, see <http://www.gnu.org/licenses/>.
*/

using Omega_Red.Managers;
using Omega_Red.ViewModels;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Input;

namespace Omega_Red.Tools
{
    public class PlayingControl : ObservableObject
    {
        private delegate bool CheckStateDelegate();

        private PCSX2Controller.StatusEnum m_Status = PCSX2Controller.StatusEnum.NoneInitilized;

        private bool m_isPaused = false;

        private string m_Message = "";
        
        public PlayingControl()
        {
            PCSX2Controller.Instance.ChangeStatusEvent += Instance_m_ChangeStatusEvent;
        }

        public string Message
        {
            get { return m_Message; }
            set
            {
                m_Message = value;
                RaisePropertyChangedEvent("Message");
            }
        }        

        void Instance_m_ChangeStatusEvent(PCSX2Controller.StatusEnum a_Status)
        {
            Status = a_Status;

            switch (a_Status)
            {
                case PCSX2Controller.StatusEnum.NoneInitilized:
                    Message = App.Current.Resources["NoneInitilizedTitle"] as String;
                    break;
                case PCSX2Controller.StatusEnum.Initilized:
                    Message = App.Current.Resources["InitilizedTitle"] as String;
                    break;
                case PCSX2Controller.StatusEnum.Stopped:
                    Message = App.Current.Resources["StoppedTitle"] as String;
                    break;
                case PCSX2Controller.StatusEnum.Paused:
                    Message = App.Current.Resources["PausedTitle"] as String;
                    break;
                case PCSX2Controller.StatusEnum.Started:
                default:
                    Message = "";
                    break;
            }

            if (a_Status == PCSX2Controller.StatusEnum.Started)
                IsPaused = false;


        }                      

        class PlayingControlCommand : ICommand
        {
            private readonly Action _action;

            private CheckStateDelegate m_CheckStateDelegate;

            public PlayingControlCommand(Action action, CheckStateDelegate a_CheckStateDelegate)
            {
                _action = action;

                m_CheckStateDelegate = a_CheckStateDelegate;
            }

            public void Execute(object parameter)
            {
                _action();
            }

            public bool CanExecute(object parameter)
            {
                if (m_CheckStateDelegate == null)
                    return false;

                return m_CheckStateDelegate();
            }

            public event EventHandler CanExecuteChanged
            {
                add { CommandManager.RequerySuggested += value; }
                remove { CommandManager.RequerySuggested -= value; }
            }
        }

        public PCSX2Controller.StatusEnum Status
        {
            get { return m_Status; }
            set
            {
                m_Status = value;
                RaisePropertyChangedEvent("Status");
            }
        }
                
        public ICommand PlayPauseCommand
        {
            get { return new PlayingControlCommand(playPauseInner, () => {
                return Status != PCSX2Controller.StatusEnum.NoneInitilized;
            });
            }
        }

        public ICommand StopCommand
        {
            get { return new PlayingControlCommand(stopInner, () => {
                return Status == PCSX2Controller.StatusEnum.Started ||
                    Status == PCSX2Controller.StatusEnum.Paused;
            });
            }
        }

        public bool IsPaused
        {
            get { return m_isPaused; }
            set
            {
                m_isPaused = value;
                RaisePropertyChangedEvent("IsPaused");
            }
        }

        public ICommand IsPausedCommand
        {
            get
            {
                return new DelegateCommand<Object>(pause);
            }
        }

        private void pause(Object aState)
        {
            if(aState is Boolean)
            {
                if ((bool)aState)
                {
                    if (m_Status == PCSX2Controller.StatusEnum.Started)
                        PCSX2Controller.Instance.PlayPause();
                }

            }
        }
        

        private void playPauseInner()
        {
            PCSX2Controller.Instance.PlayPause();
        }

        private void stopInner()
        {
            LockScreenManager.Instance.show();

            PCSX2Controller.Instance.Stop();
        }
    }
}
