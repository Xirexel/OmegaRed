﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace Golden_Phi.Managers
{
    internal class LockScreenManager
    {
        public enum Status
        {
            None,
            Show
        }

        public event Action<Status> StatusEvent;

        public event Action<string> MessageEvent;

        private Image mIconImage = null;

        private static LockScreenManager m_Instance = null;

        public static LockScreenManager Instance { get { if (m_Instance == null) m_Instance = new LockScreenManager(); return m_Instance; } }

        private LockScreenManager()
        {
            try
            {
                mIconImage = new Image();

                BitmapImage lBitmapImage = new BitmapImage();
                lBitmapImage.BeginInit();
                lBitmapImage.UriSource = new Uri("pack://application:,,,/Golden Phi;component/Assests/Images/Golden_Phi.gif", UriKind.Absolute);
                lBitmapImage.EndInit();

                WpfAnimatedGif.ImageBehavior.SetAnimatedSource(mIconImage, lBitmapImage);

                if (mIconImage != null)
                    mIconImage.Loaded += (object sender, RoutedEventArgs e) =>
                    {
                        var l_Controller = WpfAnimatedGif.ImageBehavior.GetAnimationController(mIconImage);

                        if (l_Controller != null)
                            l_Controller.Pause();
                    };
            }
            catch (Exception)
            { }
        }

        public Image IconImage { get { return mIconImage; } }
        
        public void hide()
        {
            Application.Current.Dispatcher.BeginInvoke(DispatcherPriority.Normal, (ThreadStart)delegate ()
            {
                if (StatusEvent != null)
                    StatusEvent(Status.None);

                if (mIconImage.Source != null)
                    WpfAnimatedGif.ImageBehavior.GetAnimationController(mIconImage).Pause();
            });
        }

        public void show()
        {
            Application.Current.Dispatcher.BeginInvoke(DispatcherPriority.Normal, (ThreadStart)delegate ()
            {
                if (StatusEvent != null)
                    StatusEvent(Status.Show);

                if(mIconImage.Source != null)
                    WpfAnimatedGif.ImageBehavior.GetAnimationController(mIconImage).Play();
            });
        }

        public void displayMessage(string aMessage)
        {
            if (MessageEvent == null)
                return;

            Application.Current.Dispatcher.BeginInvoke(DispatcherPriority.Background, (ThreadStart)delegate ()
            {
                MessageEvent(aMessage);
            });
        }
    }
}
